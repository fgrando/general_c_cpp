#include <stdio.h>
#include <stdint.h>

#ifdef FEATURE_ALPHA
#define FEATURE_ALPHA_DEFINED (0x1 << 0)
#else
#define FEATURE_ALPHA_DEFINED 0x0
#endif

#ifdef FEATURE_BETA
#define FEATURE_BETA_DEFINED (0x1 << 8)
#else
#define FEATURE_BETA_DEFINED 0x0
#endif

#ifdef FEATURE_GAMMA
#define FEATURE_GAMMA_DEFINED (0x1 << 16)
#else
#define FEATURE_GAMMA_DEFINED 0x0
#endif

#ifdef FEATURE_DELTA
#define FEATURE_DELTA_DEFINED (0x1 << 24)
#else
#define FEATURE_DELTA_DEFINED 0x0
#endif

#define BF_VALUES ( (uint32_t) (   \
    FEATURE_ALPHA_DEFINED |        \
    FEATURE_BETA_DEFINED  |        \
    FEATURE_GAMMA_DEFINED |        \
    FEATURE_DELTA_DEFINED          \
    ))

double GLOBALVAR1 = 0.4;
float GLOBALVAR2 = 0.5;
const uint32_t BUILD_FEATURES = BF_VALUES;
uint32_t GLOBALVAR3 = 3;

int main()
{
    printf("feature BF: %d %08X\n", BUILD_FEATURES, BUILD_FEATURES);
    printf("variable location and value can be viewed with: objdump -x build-features  | grep -i build_features\n");
    return 0;
}
