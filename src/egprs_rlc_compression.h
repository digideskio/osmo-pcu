/* egprs_rlc_compression.h
 *  Routines for EGPRS RLC bitmap compression handling
 */

#pragma once

struct egprs_compress_node;

/* Singleton to manage the EGPRS compression algorithm. */
class egprs_compress
{
public:
	static int decompress_crbb(int8_t compress_bmap_len,
		bool start, const uint8_t *orig_buf,
		bitvec *dest);
	egprs_compress();

private:
	egprs_compress_node *ones_list;
	egprs_compress_node *zeros_list;

	void decode_tree_init(void);
	static egprs_compress *s_instance;
	static egprs_compress*instance();
	egprs_compress_node *create_tree_node(void *);
	void build_codewords(egprs_compress_node *root, const char *cdwd[]);
	/* singleton class, so this private destructor is left unimplemented. */
	~egprs_compress();
};
