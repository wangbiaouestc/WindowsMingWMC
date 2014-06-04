#include <spu_mfcio.h>
#include "h264_dma.h"

DECLARE_ALIGNED_16(dma_list_elem_t, put_list_buf[2*(52+26+26)]);
dma_list_elem_t* put_list;

DECLARE_ALIGNED_16(dma_list_elem_t, get_list_buf[16*(4+5 + 2*3)]);
dma_list_elem_t* get_list;

inline void spu_dma_get(void *ls, unsigned ea, int size, int tag){
	mfc_get(ls, ea, size, tag, 0, 0);
}

inline void spu_dma_put(void *ls, unsigned ea, int size, int tag){
	mfc_put(ls, ea, size, tag, 0, 0);
}

inline void spu_dma_barrier_put(void *ls, unsigned ea, int size, int tag){
	mfc_putb(ls, ea, size, tag, 0, 0);
}

// Function that wait to finish a DMA transfer with especific id
inline void wait_dma_id(int id){
	spu_writech(MFC_WrTagMask, 1<< id);
	(void)spu_mfcstat(MFC_TAG_UPDATE_ALL);
}

// Functions to get/put a block from/to main memory
void get_dma_list(void *dst, void* ea, unsigned int w, unsigned int h, unsigned int stride, unsigned int tag, int barrier)
{
    unsigned int i = 0;
    unsigned int listsize;
    unsigned int ea_low;

	dma_list_elem_t* list = get_list;
	get_list+=h;

    ea_low=(uint32_t) mfc_ea2l(ea);

    /* Create the list, size of each list id the "width" parameter defined by the user */
    for ( i=0; i<h; i++ ){
        list[i].size.all32 = w;
        list[i].ea_low = ea_low;
        ea_low += stride;
    }
    /* Specify the list size and initiate the list transfer */
    listsize = h*sizeof(dma_list_elem_t);
    if (barrier)
		mfc_getlb(dst, (unsigned)ea, list, listsize, tag, 0, 0);
	else
		mfc_getl(dst, (unsigned)ea, list, listsize, tag, 0, 0);
}


void put_dma_list(void *src, void* ea, unsigned int size, unsigned int h, unsigned int stride, unsigned int tag){
    unsigned int i = 0;
    unsigned int listsize;
    unsigned int ea_low;

	dma_list_elem_t* list = put_list;
	put_list+=h;

	ea_low=(uint32_t) mfc_ea2l(ea);

    /* Create the list, size of each list id the "width" parameter defined by the user */
    for ( i=0; i<h; i++ ) {
        list[i].size.all32 = size;
        list[i].ea_low = ea_low;
        ea_low += stride;
    }
    /* Specify the list size and initiate the list transfer */
    listsize = h*sizeof(dma_list_elem_t);
	mfc_putl(src, (unsigned) ea, list, listsize, tag, 0, 0);
}
