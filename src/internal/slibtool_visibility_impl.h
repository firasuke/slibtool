#ifndef SLIBTOOL_VISIBILITY_IMPL_H
#define SLIBTOOL_VISIBILITY_IMPL_H

#ifdef _ATTR_VISIBILITY_HIDDEN
#define slbt_hidden _ATTR_VISIBILITY_HIDDEN
#else
#define slbt_hidden
#endif

#endif
