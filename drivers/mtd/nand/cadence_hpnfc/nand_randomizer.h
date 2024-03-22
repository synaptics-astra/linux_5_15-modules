#ifndef __NAND_RANDOMIZER_H__
#define __NAND_RANDOMIZER_H__

#include <linux/types.h>

struct nand_randomizer {
	uint8_t *data;
	uint32_t length;
	uint32_t mask;
	uint32_t pageinblock_mask;
	uint32_t erasesize;
	uint32_t writesize;
	uint32_t oobsize;
	int page_start;
};

#ifdef CONFIG_MTD_NAND_RANDOMIZER
void nand_randomize_page(struct nand_randomizer *randomizer,
			 uint8_t *dat, uint8_t *oob, int page);

int nand_randomize_init(struct nand_randomizer *randomizer, uint32_t erasesize,
			uint32_t writesize, uint32_t oobsize,
			uint8_t *data, unsigned int length,
			int page_start);
#else
static inline void
nand_randomize_page(struct nand_randomizer *randomizer,
		    uint8_t *dat, uint8_t *oob, int page)
{
}

static int
nand_randomize_init(struct nand_randomizer *randomizer, uint32_t erasesize,
		    uint32_t writesize, uint32_t oobsize,
		    uint8_t *data, unsigned int length,
		    int page_start)
{
	return 0;
}
#endif

#endif /* __NAND_RANDOMIZER_H__ */
