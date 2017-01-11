#ifndef DASH__VIEW__INDEX_SET_H__INCLUDED
#define DASH__VIEW__INDEX_SET_H__INCLUDED

#include <dash/view/ViewTraits.h>
#include <dash/view/Origin.h>
#include <dash/view/Local.h>
#include <dash/view/SetUnion.h>

#include <dash/pattern/PatternProperties.h>



namespace dash {

namespace detail {

enum index_scope {
  local_index,
  global_index
};

template <
  class       IndexType,
  index_scope IndexScope >
struct scoped_index {
  static const index_scope scope = IndexScope;
  IndexType                value;
};

} // namespace detail


template <class IndexType>
using local_index_t
  = dash::detail::scoped_index<IndexType, dash::detail::local_index>;

template <class IndexType>
using global_index_t
  = dash::detail::scoped_index<IndexType, dash::detail::global_index>;


// Forward-declarations

template <class ViewTypeA, class ViewTypeB>
constexpr ViewTypeA
intersect(
  const ViewTypeA & va,
  const ViewTypeB & vb);

template <class IndexSetType, class ViewType>
class IndexSetBase;

template <class ViewType>
class IndexSetIdentity;

template <class ViewType>
class IndexSetLocal;

template <class ViewType>
class IndexSetGlobal;

template <class ViewType>
class IndexSetSub;



template <class ViewType>
const typename ViewType::index_set_type &
index(const ViewType & v) {
  return v.index_set();
}

#ifdef __TODO__
template <class ContainerType>
constexpr
typename std::enable_if <
  !dash::view_traits<ContainerType>::is_view::value,
  IndexSetIdentity<ContainerType>
>::type
index(const ContainerType & c) {
  return IndexSetIdentity<ContainerType>(c);
}
#endif


namespace detail {

template <
  class IndexSetType,
  int   BaseStride   = 1 >
class IndexSetIterator {
  typedef IndexSetIterator<IndexSetType>        self_t;
  typedef typename IndexSetType::index_type index_type;
public:
  constexpr IndexSetIterator(
    const IndexSetType & index_set,
    index_type           position,
    index_type           stride    = BaseStride)
  : _index_set(index_set), _pos(position), _stride(stride)
  { }

  constexpr index_type operator*() const {
    return _pos < _index_set.size()
              ? _index_set[_pos]
              : _index_set[_pos-1] + 1;
  }

  self_t & operator++() {
    _pos += _stride;
    return *this;
  }

  self_t & operator--() {
    _pos -= _stride;
    return *this;
  }

  constexpr self_t operator++(int) const {
    return self_t(_index_set, _pos + _stride, _stride);
  }

  constexpr self_t operator--(int) const {
    return self_t(_index_set, _pos - _stride, _stride);
  }

  self_t & operator+=(int i) {
    _pos += i * _stride;
    return *this;
  }

  self_t & operator-=(int i) {
    _pos -= i * _stride;
    return *this;
  }

  constexpr self_t operator+(int i) const {
    return self_t(_index_set, _pos + (i * _stride), _stride);
  }

  constexpr self_t operator-(int i) const {
    return self_t(_index_set, _pos - (i * _stride), _stride);
  }

  constexpr index_type pos() const {
    return _pos;
  }

  constexpr bool operator==(const self_t & rhs) const {
    return _pos == rhs._pos && _stride == rhs._stride;
  }

  constexpr bool operator!=(const self_t & rhs) const {
    return not (*this == rhs);
  }

private:
  const IndexSetType & _index_set;
  index_type           _pos;
  index_type           _stride = BaseStride;
};

} // namespace detail

// -----------------------------------------------------------------------


/* NOTE: Local and global mappings of index sets should be implemented
 *       without IndexSet member functions like this:
 *
 *       dash::local(index_set) {
 *         return dash::index(
 *                  // map the index set's view to local type, not the
 *                  // index set itself:
 *                  dash::local(index_set.view())
 *                );
 *
 */


template <
  class IndexSetType,
  class ViewType >
constexpr auto
local(
  const IndexSetBase<IndexSetType, ViewType> & index_set
) -> decltype(index_set.local()) {
  return index_set.local();
}



template <
  class IndexSetType,
  class ViewType >
class IndexSetBase
{
public:
  typedef typename ViewType::index_type
    index_type;
  typedef typename dash::view_traits<ViewType>::origin_type
    origin_type;
  typedef typename dash::view_traits<ViewType>::domain_type
    view_domain_type;
  typedef typename ViewType::local_type
    view_local_type;
  typedef typename dash::view_traits<ViewType>::global_type
    view_global_type;
  typedef typename dash::view_traits<view_domain_type>::index_set_type
    index_set_domain_type;
  typedef typename origin_type::pattern_type
    pattern_type;
  typedef typename dash::view_traits<view_local_type>::index_set_type
    local_type;
  typedef typename dash::view_traits<view_global_type>::index_set_type
    global_type;
public:
  typedef detail::IndexSetIterator<IndexSetType> iterator;

  constexpr explicit IndexSetBase(const ViewType & view)
  : _view(view), _pattern(dash::origin(view).pattern())
  { }

  constexpr iterator begin() const {
    return iterator(*static_cast<const IndexSetType *>(this), 0);
  }

  constexpr iterator end() const {
    return iterator(*static_cast<const IndexSetType *>(this),
                    static_cast<const IndexSetType *>(this)->size());
  }

  iterator begin() {
    return iterator(*static_cast<IndexSetType *>(this), 0);
  }

  iterator end() {
    return iterator(*static_cast<IndexSetType *>(this),
                    static_cast<IndexSetType *>(this)->size());
  }

  constexpr const ViewType & view() const {
    return _view;
  }

  constexpr const index_set_domain_type & domain() const {
    return dash::index(dash::domain(_view));
  }

  constexpr const pattern_type & pattern() const {
    return _pattern;
  }

  constexpr const index_set_domain_type & pre() const {
    return this->domain();
  }

  /*
   *  dash::index(r(10..100)).step(2)[8]  -> 26
   *  dash::index(r(10..100)).step(-5)[4] -> 80
   */
  constexpr iterator step(index_type stride) const {
    return (
      stride > 0
        ? iterator(*static_cast<const IndexSetType *>(this), 0, stride)
        : iterator(*static_cast<const IndexSetType *>(this),
                   static_cast<const IndexSetType *>(this)->size(), stride)
    );
  }

protected:
  const ViewType     & _view;
  const pattern_type & _pattern;
};

// -----------------------------------------------------------------------

template <class ViewType>
constexpr const IndexSetIdentity<ViewType> &
local(
  const IndexSetIdentity<ViewType> & index_set
) {
  return index_set;
}

template <class ViewType>
class IndexSetIdentity
: public IndexSetBase<
           IndexSetIdentity<ViewType>,
           ViewType >
{
  typedef IndexSetIdentity<ViewType>                            self_t;
  typedef IndexSetBase<self_t, ViewType>                        base_t;
public:
  typedef typename ViewType::index_type                     index_type;

  constexpr explicit IndexSetIdentity(
    const ViewType & view)
  : base_t(view)
  { }

  constexpr index_type operator[](index_type image_index) const {
    return image_index;
  }

  constexpr index_type size() const {
    return dash::domain(*this).size();
  }
};

// -----------------------------------------------------------------------

template <class ViewType>
constexpr auto
local(
  const IndexSetSub<ViewType> & index_set
) -> decltype(index_set.local()) {
  return index_set.local();
}

template <class ViewType>
constexpr auto
global(
  const IndexSetSub<ViewType> & index_set
) -> decltype(index_set.global()) {
  return index_set.global();
}

template <class ViewType>
class IndexSetSub
: public IndexSetBase<
           IndexSetSub<ViewType>,
           ViewType >
{
  typedef IndexSetSub<ViewType>                                 self_t;
  typedef IndexSetBase<self_t, ViewType>                        base_t;
public:
  typedef typename ViewType::index_type                     index_type;
  typedef typename base_t::view_domain_type           view_domain_type;
  typedef typename base_t::local_type                       local_type;
  typedef typename base_t::global_type                     global_type;
  typedef typename base_t::iterator                           iterator;
//typedef typename base_t::index_set_domain_type index_set_domain_type;
  typedef IndexSetSub<ViewType>                          preimage_type;

  constexpr IndexSetSub(
    const ViewType   & view,
    index_type         begin,
    index_type         end)
  : base_t(view),
    _domain_begin_idx(begin),
    _domain_end_idx(end)
  { }

  constexpr index_type operator[](index_type image_index) const {
//  TODO:
//  return this->domain()[_domain_begin_idx + image_index];
    return (_domain_begin_idx + image_index);
  }

  constexpr index_type size() const {
    return std::min<index_type>(
             (_domain_end_idx - _domain_begin_idx),
       // TODO:
       //    this->domain().size()
             (_domain_end_idx - _domain_begin_idx)
           );
  }

  constexpr const local_type & local() const {
    return dash::index(dash::local(this->view()));
  }

  constexpr const global_type & global() const {
    return dash::index(dash::global(this->view()));
  }

  constexpr preimage_type pre() const {
    return preimage_type(
             this->view(),
             -_domain_begin_idx,
             -_domain_begin_idx + this->view().size()
           );
  }

private:
  index_type _domain_begin_idx;
  index_type _domain_end_idx;
};

// -----------------------------------------------------------------------

template <
  class ViewType >
constexpr const IndexSetLocal<ViewType> &
local(
  const IndexSetLocal<ViewType> & index_set) {
  return index_set;
}

template <
  class ViewType >
constexpr const IndexSetGlobal<ViewType> &
global(
  const IndexSetLocal<ViewType> & index_set) {
  return index_set.global();
}

template <class ViewType>
class IndexSetLocal
: public IndexSetBase<
           IndexSetLocal<ViewType>,
           ViewType >
{
  typedef IndexSetLocal<ViewType>                               self_t;
  typedef IndexSetBase<self_t, ViewType>                        base_t;
public:
  typedef typename ViewType::index_type                     index_type;
   
  typedef self_t                                            local_type;
  typedef IndexSetGlobal<ViewType>                         global_type;
  typedef global_type                                    preimage_type;

  typedef typename base_t::iterator                           iterator;
  typedef typename base_t::pattern_type                   pattern_type;
  
  typedef dash::local_index_t<index_type>             local_index_type;
  typedef dash::global_index_t<index_type>           global_index_type;
  
  constexpr explicit IndexSetLocal(const ViewType & view)
  : base_t(view)
  { }

public:
  constexpr index_type
  operator[](index_type local_index) const {
    return this->pattern().global(
             local_index +
             // actually only required if local of sub
             this->pattern().at(
               std::max<index_type>(
                 this->pattern().global(0),
                 this->domain()[0]
               )
             )
           );
  }

  constexpr index_type size() const {
    typedef typename dash::pattern_partitioning_traits<pattern_type>::type
            pat_partitioning_traits;

    static_assert(
        pat_partitioning_traits::rectangular,
        "index sets for non-rectangular patterns are not supported yet");

    return (
        ( pat_partitioning_traits::minimal ||
          this->pattern().blockspec().size()
            <= this->pattern().team().size() )
        // blocked (not blockcyclic) distribution: single local
        // element space with contiguous global index range
        ? std::min<index_type>(
            this->pattern().local_size(),
            this->domain().size()
          )
        // blockcyclic distribution: local element space chunked
        // in global index range
        : this->pattern().local_size() + // <-- TODO: intersection of local
          this->domain().pre()[0]        //           blocks and domain
    );
  }

  constexpr const local_type & local() const {
    return *this;
  }

  constexpr global_type global() const {
    return global_type(this->view());
  }

  constexpr preimage_type pre() const {
    return preimage_type(this->view());
  }
};

// -----------------------------------------------------------------------

template <
  class ViewType >
constexpr const IndexSetLocal<ViewType> &
local(
  const IndexSetGlobal<ViewType> & index_set) {
  return index_set.local();
}

template <
  class ViewType >
constexpr const IndexSetGlobal<ViewType> &
global(
  const IndexSetGlobal<ViewType> & index_set) {
  return index_set;
}

template <class ViewType>
class IndexSetGlobal
: public IndexSetBase<
           IndexSetGlobal<ViewType>,
           ViewType >
{
  typedef IndexSetGlobal<ViewType>                              self_t;
  typedef IndexSetBase<self_t, ViewType>                        base_t;
public:
  typedef typename ViewType::index_type                     index_type;
   
  typedef IndexSetLocal<self_t>                             local_type;
  typedef self_t                                           global_type;
  typedef local_type                                     preimage_type;

  typedef typename base_t::iterator                           iterator;
  
  typedef dash::local_index_t<index_type>             local_index_type;
  typedef dash::global_index_t<index_type>           global_index_type;
  
  constexpr explicit IndexSetGlobal(const ViewType & view)
  : base_t(view)
  { }

public:
  constexpr index_type
  operator[](index_type global_index) const {
    return this->pattern().at(
             global_index
           );
  }

  inline index_type size() const {
    return std::max<index_type>(
             this->pattern().size(),
             this->domain().size()
           );
  }

  constexpr const local_type & local() const {
    return local();
  }

  constexpr const global_type & global() const {
    return *this;
  }

  constexpr const preimage_type & pre() const {
    return dash::index(dash::local(this->view()));
  }
};

} // namespace dash

#endif // DASH__VIEW__INDEX_SET_H__INCLUDED