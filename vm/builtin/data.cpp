#include "builtin/data.hpp"
#include "builtin/class.hpp"
#include "objectmemory.hpp"
#include "object_utils.hpp"

#include "gc/gc.hpp"

#include "capi/capi.hpp"

#include "ontology.hpp"

namespace rubinius {

  void Data::init(STATE) {
    GO(data).set(ontology::new_class(state, "Data", G(object)));
    G(data)->set_object_type(state, DataType);
  }

  Data* Data::create(STATE, void* data_ptr, Data::MarkFunctor mark, Data::FreeFunctor free) {
    Data* data;

    data = state->new_object<Data>(G(data));
    data->freed_ = false;

    // Data is just a heap alias for the handle, so go ahead and create
    // the handle and populate it as an RData now.
    capi::Handle* handle = data->handle(state);

    assert(!handle && "can't already have a handle, it's brand new!");

    handle = state->shared().add_global_handle(state, data);

    // Don't call ->ref() on handle! We don't want the handle to keep the object
    // alive by default. The handle needs to have the lifetime of the object.

    RDataShadow* rdata = reinterpret_cast<RDataShadow*>(handle->as_rdata(0));

    rdata->data = data_ptr;
    rdata->dmark = mark;
    rdata->dfree = free;

    data->internal_ = rdata;

    if(mark || free) {
      state->memory()->needs_finalization(data, (FinalizerFunction)&Data::finalize);
    }

    return data;
  }

  RDataShadow* Data::slow_rdata(STATE) {
    capi::Handle* handle = this->handle(state);

    assert(handle && handle->is_rdata() && "invalid initialized Data object");

    return reinterpret_cast<RDataShadow*>(handle->as_rdata(0));
  }

  void* Data::data(STATE) {
    return rdata(state)->data;
  }

  Data::FreeFunctor Data::free(STATE) {
    return rdata(state)->dfree;
  }

  Data::MarkFunctor Data::mark(STATE) {
    return rdata(state)->dmark;
  }

  void Data::finalize(STATE, Data* data) {
    capi::Handle* handle = data->handle(state);

    if(!handle->valid_p()) {
      std::cerr << "Data::finalize: object has invalid handle!" << std::endl;
      return;
    }

    if(handle->object() != data) {
      std::cerr << "Data::finalize: handle does not reference object!" << std::endl;
      return;
    }

    if(data->freed_p()) {
      // TODO: Fix the issue of finalizer ordering.
      // std::cerr << "Data::finalize called for already freed object" << std::endl;
      return;
    }

    // MRI only calls free if the data_ptr is not NULL.
    if(void* data_ptr = data->data(state)) {
      Data::FreeFunctor f = data->free(state);
      if(f) {
        // If the user specifies -1, then we call free. We check here rather
        // than when Data_Make_Struct is called because the user is allowed to
        // change dfree.
        if(reinterpret_cast<intptr_t>(f) == -1) {
          ::free(data_ptr);
        } else {
          f(data_ptr);
        }
      }
      data->set_freed();
    }
  }

  void Data::Info::mark(Object* t, ObjectMark& mark) {
    auto_mark(t, mark);

    Data* data = force_as<Data>(t);

    if(data->freed_p()) {
      // TODO: Fix the issue of finalizer ordering.
      // std::cerr << "Data::Info::mark called for already freed object" << std::endl;
      return;
    }

    RDataShadow* rdata = data->rdata();

    if(rdata->dmark) {
      ObjectMark* cur = capi::current_mark();
      capi::set_current_mark(&mark);

      (*rdata->dmark)(rdata->data);

      capi::set_current_mark(cur);
    }
  }

}
