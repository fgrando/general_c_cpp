#include <stdint.h>

union bitfieldExample {
	uint8_t value;
	struct {
		uint8_t item1:1;
		uint8_t item2:1;
		uint8_t item3:1;
		uint8_t item4:1;
		uint8_t item5:1;
		uint8_t item6:1;
		uint8_t item7:1;
		uint8_t item8:1;
	};
};
