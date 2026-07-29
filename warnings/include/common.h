#ifndef COMMON_H
#define COMMON_H
/* A static function defined in a header: unused in each TU that includes it,
   so it warns once per including file -> same offender header, different built_file. */
static int helper_unused(void)
{
    return 42;
}
#endif
