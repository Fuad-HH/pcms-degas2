#ifndef WDMCPL_INCLUSIVE_SCAN_H
#define WDMCPL_INCLUSIVE_SCAN_H
namespace wdmcpl {
  template <typename InputIt, typename OutputIt>
  OutputIt inclusive_scan(InputIt first, InputIt last, OutputIt d_first) {
    typename InputIt::value_type last_val = 0;
    for(auto it = first; it!=last; ++it) {
      last_val = last_val + *it;
      *d_first = last_val;
      ++d_first;
    }
  return d_first;
  }
}
#endif