#ifndef DASH__UTIL__LOCALITY_DOMAIN_H__INCLUDED
#define DASH__UTIL__LOCALITY_DOMAIN_H__INCLUDED

#include <dash/util/Locality.h>

#include <dash/dart/if/dart_types.h>
#include <dash/dart/if/dart_locality.h>

#include <dash/algorithm/internal/String.h>

#include <dash/Exception.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
#include <iterator>
#include <algorithm>
#include <functional>


namespace dash {
namespace util {

/**
 * Wrapper of a single \c dart_domain_locality_t object.
 *
 * Usage examples:
 *
 * \code
 * dash::util::TeamLocality     team_locality(dash::Team::All());
 * dash::util::LocalityDomain & domain = team_locality.domain();
 *
 * domain.split_groups(dash::util::Locality::Scope::Module);
 *
 * for (auto part : domain.groups()) {
 *   // Iterate over all domains in Module locality scope
 * }
 * \endcode
 */
class LocalityDomain
{
private:
  typedef LocalityDomain                       self_t;
  typedef dash::util::Locality::Scope         Scope_t;

private:
  /**
   * Iterator on subdomains of the locality domain.
   */
  template< typename LocalityDomainT >
  class domain_iterator
  : public std::iterator< std::random_access_iterator_tag, LocalityDomainT >
  {
    template< typename LocalityDomainT_ >
    friend class domain_iterator;

  private:
    typedef domain_iterator<LocalityDomainT>          self_t;

  public:
    typedef domain_iterator<LocalityDomainT>       self_type;
    typedef int                              difference_type;

    typedef       LocalityDomainT                 value_type;
    typedef       LocalityDomainT *                  pointer;
    typedef       LocalityDomainT &                reference;

  public:

    domain_iterator(
      LocalityDomainT & domain,
      int               subdomain_idx = 0)
    : _domain(&domain),
      _idx(subdomain_idx)
    { }

    domain_iterator() = default;

    template< typename LocalityDomainT_Other >
    domain_iterator(
      const domain_iterator<LocalityDomainT_Other> & other)
    : _domain(const_cast<LocalityDomainT *>(other._domain)),
      _idx(other._idx)
    { }

    bool operator==(const self_type & rhs) const {
      return *_domain == *(rhs._domain) && _idx == rhs._idx;
    }

    bool operator!=(const self_type & rhs) const {
      return !(*this == rhs);
    }

    reference operator[](int i)
    {
      DASH_ASSERT(_domain != nullptr);
      int subdomain_idx = _idx + i;
      return _domain->at(subdomain_idx);
    }

    reference operator*()
    {
      DASH_ASSERT(_domain != nullptr);
      return _domain->at(_idx);
    }

    pointer operator->()
    {
      DASH_ASSERT(_domain != nullptr);
      return &(_domain->at(_idx));
    }

    self_t & operator++()      { _idx++;   return *this; }
    self_t & operator--()      { _idx--;   return *this; }
    self_t & operator+=(int i) { _idx+= i; return *this; }
    self_t & operator-=(int i) { _idx-= i; return *this; }
    self_t   operator+(int i)  { self_t ret = *this; ret += i; return ret; }
    self_t   operator-(int i)  { self_t ret = *this; ret -= i; return ret; }
    self_t   operator++(int)   { self_t ret = *this; _idx++;   return ret; }
    self_t   operator--(int)   { self_t ret = *this; _idx--;   return ret; }

  private:
    LocalityDomainT * _domain = nullptr;
    int               _idx    = 0;

  }; // class LocalityDomain::iterator

public:

  typedef domain_iterator<self_t>                iterator;
  typedef domain_iterator<const self_t>    const_iterator;

public:

  inline LocalityDomain()
  : _domain(nullptr),
    _subdomains(nullptr)
  {
    clear();
  }

  LocalityDomain(
    const self_t           & parent,
    dart_domain_locality_t * domain);

  LocalityDomain(
    dart_domain_locality_t * domain);

  LocalityDomain(
    const self_t           & parent,
    std::string              subdomain_tag);

  ~LocalityDomain();

  LocalityDomain(
    const self_t & other);

  self_t & operator=(
    const self_t & other);

  inline bool operator==(
    const self_t & rhs) const
  {
    return ( (_domain == rhs._domain)
             ||
             ( (_domain     != nullptr &&
                rhs._domain != nullptr)
                &&
               (_domain->team
                  == rhs._domain->team &&
                _domain->domain_tag
                  == rhs._domain->domain_tag) ) );
  }

  inline bool operator!=(
    const self_t & rhs) const
  {
    return !(*this == rhs);
  }

  /**
   * Remove subdomains that do not match one of the specified domain tags
   * and are not a subdomain of a matched domain.
   */
  self_t & select(
    std::initializer_list<std::string> subdomain_tags);

  /**
   * Remove subdomains that match the specified domain tags or are a
   * subdomain of a matched domain.
   */
  self_t & exclude(
    std::initializer_list<std::string> subdomain_tags);

  /**
   * Add a group subdomain consisting of domains with the specified tags.
   */
  self_t & group(
    std::initializer_list<std::string> group_subdomain_tags);

  /**
   * Split locality domain into given number of parts at specified scope.
   */
  self_t & split(
    dash::util::Locality::Scope        scope,
    int                                num_split_parts);

  /**
   * Lazy-load instance of \c LocalityDomain for child subdomain at relative
   * index.
   */
  self_t & at(
    int relative_index) const;

  /**
   * Find locality subdomain with specified domain tag.
   */
  iterator find(
    const std::string & subdomain_tag);

  inline std::vector<iterator> & groups()
  {
    return _groups;
  }

  inline const std::vector<iterator> & groups() const
  {
    return _groups;
  }

  inline std::vector<self_t> & parts()
  {
    return _parts;
  }

  inline const std::vector<self_t> & parts() const
  {
    return _parts;
  }

  inline dart_team_t dart_team()
  {
    if (nullptr == _domain) {
      return DART_TEAM_NULL;
    }
    return _domain->team;
  }

  inline const std::string & domain_tag() const
  {
    return _domain_tag;
  }

  inline const dart_domain_locality_t & dart_type() const
  {
    DASH_ASSERT(_domain != nullptr);
    return *_domain;
  }

  inline std::string host() const
  {
    DASH_ASSERT(_domain != nullptr);
    return _domain->host;
  }

  inline iterator begin()
  {
    return _begin;
  }

  inline const_iterator begin() const
  {
    return const_iterator(_begin);
  }

  inline iterator end()
  {
    return _end;
  }

  inline const_iterator end() const
  {
    return const_iterator(_end);
  }

  inline size_t size() const
  {
    return _domain == nullptr
           ? 0
           : _domain->num_domains;
  }

  inline const std::vector<dart_unit_t> & units() const
  {
    return _unit_ids;
  }

  inline const dart_hwinfo_t & hwinfo() const
  {
    DASH_ASSERT(_domain != nullptr);
    return _domain->hwinfo;
  }

  inline int level() const
  {
    return (nullptr == _domain ? -1 : _domain->level);
  }

  inline Scope_t scope() const
  {
    return _domain == nullptr
           ? Scope_t::Undefined
           : static_cast<Scope_t>(_domain->scope);
  }

  inline int node_id() const
  {
    return (nullptr == _domain ? -1 : _domain->node_id);
  }

  inline int num_nodes() const
  {
    return (nullptr == _domain ? -1 : _domain->num_nodes);
  }

  inline int relative_index() const
  {
    return (nullptr == _domain ? -1 : _domain->relative_index);
  }

private:

  inline void init(
    dart_domain_locality_t * domain)
  {
    DASH_LOG_DEBUG("LocalityDomain.init()",
                   "domain:", domain->domain_tag);

    clear();

    _domain     = domain;
    _domain_tag = _domain->domain_tag;

    if (_domain->num_units > 0) {
      _unit_ids.insert(_unit_ids.end(),
                       _domain->unit_ids,
                       _domain->unit_ids + _domain->num_units);
    }

    _begin = iterator(*this, 0);
    _end   = iterator(*this, _domain->num_domains);

    collect_groups(*this);

    DASH_LOG_DEBUG("LocalityDomain.init >",
                   "domain:", domain->domain_tag);
  }

  inline void clear()
  {
    return;

    DASH_LOG_DEBUG("LocalityDomain.clear()");

    _unit_ids.clear();
    _groups.clear();
    _parts.clear();
    if (nullptr != _subdomains) {
      _subdomains->clear();
    }
    _begin = iterator(*this, 0);
    _end   = iterator(*this, 0);

    DASH_LOG_DEBUG("LocalityDomain.clear >");
  }

  inline void collect_groups(
    self_t & domain)
  {
#if 0
    for (auto it = domain.begin(); it != domain.end(); ++it) {
      self_t & subdomain = *it;
      if (subdomain.scope() == Scope_t::Group) {
        _groups.push_back(it);
      }
      collect_groups(subdomain);
    }
#else
    _groups.clear();
    for (auto gdt : _group_domain_tags) {
      _groups.push_back(find(gdt));
    }
#endif
  }

private:
  /// Underlying \c dart_domain_locality_t object.
  dart_domain_locality_t                    * _domain    = nullptr;
  /// Copy of _domain->domain_tag to avoid string copying.
  std::string                                 _domain_tag;
  /// Cache of lazy-loaded subdomains, mapped by subdomain relative index.
  /// Must be heap-allocated as type is incomplete due to type definition
  /// cycle.
  mutable std::unordered_map<int, self_t>   * _subdomains = nullptr;
  /// Units in the domain.
  std::vector<dart_unit_t>                    _unit_ids;
  /// Iterator to the first subdomain.
  iterator                                    _begin;
  /// Iterator past the last subdomain.
  iterator                                    _end;
  /// Whether this instance is owner of _domain.
  bool                                        _is_owner   = false;
  /// Domain tags of groups in the locality domain.
  std::vector<iterator>                       _groups;
  std::vector<std::string>                    _group_domain_tags;
  /// Split domains in the team locality, one domain for every split group.
  std::vector<self_t>                         _parts;

}; // class LocalityDomain

} // namespace util
} // namespace dash

#endif // DASH__UTIL__LOCALITY_DOMAIN_H__INCLUDED
