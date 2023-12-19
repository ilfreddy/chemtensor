/// \file block_sparse_tensor.c
/// \brief Block-sparse tensor structure.

#include <memory.h>
#include <cblas.h>
#include "block_sparse_tensor.h"
#include "util.h"


//________________________________________________________________________________________________________________________
///
/// \brief Temporary data structure for enumerating quantum numbers and their multiplicities.
///
struct qnumber_count
{
	qnumber qnum;
	int count;
};

//________________________________________________________________________________________________________________________
///
/// \brief Comparison function for sorting.
///
static int compare_qnumber_count(const void* a, const void* b)
{
	const struct qnumber_count* x = (const struct qnumber_count*)a;
	const struct qnumber_count* y = (const struct qnumber_count*)b;
	// assuming that quantum numbers are distinct
	return x->qnum - y->qnum;
}

//________________________________________________________________________________________________________________________
///
/// \brief Allocate memory for a block-sparse tensor, including the dense blocks for conserved quantum numbers.
///
void allocate_block_sparse_tensor(const int ndim, const long* restrict dim, const enum tensor_axis_direction* axis_dir, const qnumber** restrict qnums, struct block_sparse_tensor* restrict t)
{
	assert(ndim >= 0);
	t->ndim = ndim;

	if (ndim == 0)  // special case
	{
		t->dim_logical = NULL;
		t->dim_blocks  = NULL;

		t->axis_dir = NULL;

		t->qnums_logical = NULL;
		t->qnums_blocks  = NULL;

		// allocate memory for a single block
		t->blocks = aligned_calloc(MEM_DATA_ALIGN, 1, sizeof(struct dense_tensor*));
		t->blocks[0] = aligned_calloc(MEM_DATA_ALIGN, 1, sizeof(struct dense_tensor));
		allocate_dense_tensor(ndim, dim, t->blocks[0]);

		return;
	}

	t->dim_logical = aligned_alloc(MEM_DATA_ALIGN, ndim * sizeof(long));
	memcpy(t->dim_logical, dim, ndim * sizeof(long));

	t->dim_blocks = aligned_calloc(MEM_DATA_ALIGN, ndim, sizeof(long));

	t->axis_dir = aligned_alloc(MEM_DATA_ALIGN, ndim * sizeof(enum tensor_axis_direction));
	memcpy(t->axis_dir, axis_dir, ndim * sizeof(enum tensor_axis_direction));

	t->qnums_logical = aligned_calloc(MEM_DATA_ALIGN, ndim, sizeof(qnumber*));
	for (int i = 0; i < ndim; i++)
	{
		t->qnums_logical[i] = aligned_alloc(MEM_DATA_ALIGN, dim[i] * sizeof(qnumber));
		memcpy(t->qnums_logical[i], qnums[i], dim[i] * sizeof(qnumber));
	}

	t->qnums_blocks = aligned_calloc(MEM_DATA_ALIGN, ndim, sizeof(qnumber*));

	// count quantum numbers along each dimension axis
	struct qnumber_count** qcounts = aligned_alloc(MEM_DATA_ALIGN, ndim * sizeof(struct qnumber_count*));
	for (int i = 0; i < ndim; i++)
	{
		assert(dim[i] > 0);

		// likely not requiring all the allocated memory
		qcounts[i] = aligned_calloc(MEM_DATA_ALIGN, dim[i], sizeof(struct qnumber_count));
		long nqc = 0;
		for (long j = 0; j < dim[i]; j++)
		{
			int k;
			for (k = 0; k < nqc; k++)
			{
				if (qcounts[i][k].qnum == qnums[i][j]) {
					qcounts[i][k].count++;
					break;
				}
			}
			if (k == nqc)
			{
				// add new entry
				qcounts[i][k].qnum = qnums[i][j];
				qcounts[i][k].count = 1;
				nqc++;
			}
		}
		assert(nqc <= dim[i]);

		qsort(qcounts[i], nqc, sizeof(struct qnumber_count), compare_qnumber_count);

		t->dim_blocks[i] = nqc;

		// store quantum numbers
		t->qnums_blocks[i] = aligned_calloc(MEM_DATA_ALIGN, nqc, sizeof(qnumber));
		for (long j = 0; j < nqc; j++) {
			t->qnums_blocks[i][j] = qcounts[i][j].qnum;
		}
	}

	// allocate dense tensor blocks
	const long nblocks = integer_product(t->dim_blocks, ndim);
	t->blocks = aligned_calloc(MEM_DATA_ALIGN, nblocks, sizeof(struct dense_tensor*));
	long* index_block = aligned_calloc(MEM_DATA_ALIGN, ndim, sizeof(long));
	for (long k = 0; k < nblocks; k++, next_tensor_index(ndim, t->dim_blocks, index_block))
	{
		// probe whether quantum numbers sum to zero
		qnumber qsum = 0;
		for (int i = 0; i < ndim; i++)
		{
			qsum += axis_dir[i] * t->qnums_blocks[i][index_block[i]];
		}
		if (qsum != 0) {
			continue;
		}

		// allocate dense tensor block
		t->blocks[k] = aligned_calloc(MEM_DATA_ALIGN, 1, sizeof(struct dense_tensor));
		long* bdim = aligned_alloc(MEM_DATA_ALIGN, ndim * sizeof(long));
		for (int i = 0; i < ndim; i++) {
			bdim[i] = qcounts[i][index_block[i]].count;
		}
		allocate_dense_tensor(ndim, bdim, t->blocks[k]);
		aligned_free(bdim);
	}
	aligned_free(index_block);

	for (int i = 0; i < ndim; i++)
	{
		aligned_free(qcounts[i]);
	}
	aligned_free(qcounts);
}


//________________________________________________________________________________________________________________________
///
/// \brief Delete a block-sparse tensor (free memory).
///
void delete_block_sparse_tensor(struct block_sparse_tensor* t)
{
	if (t->ndim == 0)  // special case
	{
		delete_dense_tensor(t->blocks[0]);
		aligned_free(t->blocks[0]);
		aligned_free(t->blocks);

		return;
	}

	// free dense tensor blocks
	const long nblocks = integer_product(t->dim_blocks, t->ndim);
	long* index_block = aligned_calloc(MEM_DATA_ALIGN, t->ndim, sizeof(long));
	for (long k = 0; k < nblocks; k++, next_tensor_index(t->ndim, t->dim_blocks, index_block))
	{
		// probe whether quantum numbers sum to zero
		qnumber qsum = 0;
		for (int i = 0; i < t->ndim; i++)
		{
			qsum += t->axis_dir[i] * t->qnums_blocks[i][index_block[i]];
		}
		if (qsum != 0) {
			continue;
		}

		assert(t->blocks[k] != NULL);
		delete_dense_tensor(t->blocks[k]);
		aligned_free(t->blocks[k]);
	}
	aligned_free(index_block);
	aligned_free(t->blocks);
	t->blocks = NULL;

	for (int i = 0; i < t->ndim; i++)
	{
		aligned_free(t->qnums_blocks[i]);
		aligned_free(t->qnums_logical[i]);
	}
	aligned_free(t->qnums_blocks);
	aligned_free(t->qnums_logical);

	aligned_free(t->axis_dir);

	aligned_free(t->dim_blocks);
	aligned_free(t->dim_logical);
}


//________________________________________________________________________________________________________________________
///
/// \brief Retrieve a dense block based on its quantum numbers.
///
struct dense_tensor* block_sparse_tensor_get_block(const struct block_sparse_tensor* t, const qnumber* qnums)
{
	assert(t->ndim > 0);

	// find indices corresponding to quantum numbers
	long* index = aligned_alloc(MEM_DATA_ALIGN, t->ndim * sizeof(long));
	for (int i = 0; i < t->ndim; i++)
	{
		index[i] = -1;
		for (long j = 0; j < t->dim_blocks[i]; j++)
		{
			if (t->qnums_blocks[i][j] == qnums[i]) {
				index[i] = j;
				break;
			}
		}
		if (index[i] == -1) {
			// quantum number not found
			return NULL;
		}
	}

	long o = tensor_index_to_offset(t->ndim, t->dim_blocks, index);

	aligned_free(index);

	return t->blocks[o];
}


//________________________________________________________________________________________________________________________
///
/// \brief Scale tensor t by alpha.
///
void scale_block_sparse_tensor(const double alpha, struct block_sparse_tensor* t)
{
	const long nblocks = integer_product(t->dim_blocks, t->ndim);
	for (long k = 0; k < nblocks; k++)
	{
		struct dense_tensor* b = t->blocks[k];
		if (b != NULL) {
			scale_dense_tensor(alpha, b);
		}
	}
}


//________________________________________________________________________________________________________________________
///
/// \brief Elementwise conjugation of a block-sparse tensor.
///
void conjugate_block_sparse_tensor(struct block_sparse_tensor* t)
{
	const long nblocks = integer_product(t->dim_blocks, t->ndim);
	for (long k = 0; k < nblocks; k++)
	{
		struct dense_tensor* b = t->blocks[k];
		if (b != NULL) {
			conjugate_dense_tensor(b);
		}
	}
}


//________________________________________________________________________________________________________________________
///
/// \brief Convert a block-sparse to an equivalent dense tensor.
///
void block_sparse_to_dense_tensor(const struct block_sparse_tensor* restrict s, struct dense_tensor* restrict t)
{
	allocate_dense_tensor(s->ndim, s->dim_logical, t);

	// for each block with matching quantum numbers...
	const long nblocks = integer_product(s->dim_blocks, s->ndim);
	long* index_block = aligned_calloc(MEM_DATA_ALIGN, s->ndim, sizeof(long));
	for (long k = 0; k < nblocks; k++, next_tensor_index(s->ndim, s->dim_blocks, index_block))
	{
		// probe whether quantum numbers sum to zero
		qnumber qsum = 0;
		for (int i = 0; i < s->ndim; i++)
		{
			qsum += s->axis_dir[i] * s->qnums_blocks[i][index_block[i]];
		}
		if (qsum != 0) {
			continue;
		}

		const struct dense_tensor* b = s->blocks[k];
		assert(b != NULL);
		assert(b->ndim == s->ndim);

		// fan-out dense to logical indices
		long** index_map = aligned_calloc(MEM_DATA_ALIGN, s->ndim, sizeof(long*));
		for (int i = 0; i < s->ndim; i++)
		{
			index_map[i] = aligned_calloc(MEM_DATA_ALIGN, b->dim[i], sizeof(long));
			long c = 0;
			for (long j = 0; j < s->dim_logical[i]; j++)
			{
				if (s->qnums_logical[i][j] == s->qnums_blocks[i][index_block[i]])
				{
					index_map[i][c] = j;
					c++;
				}
			}
			assert(c == b->dim[i]);
		}

		// distribute dense tensor entries
		long* index_b = aligned_calloc(MEM_DATA_ALIGN, b->ndim, sizeof(long));
		long* index_t = aligned_calloc(MEM_DATA_ALIGN, t->ndim, sizeof(long));
		const long nelem = dense_tensor_num_elements(b);
		for (long j = 0; j < nelem; j++, next_tensor_index(b->ndim, b->dim, index_b))
		{
			for (int i = 0; i < b->ndim; i++)
			{
				index_t[i] = index_map[i][index_b[i]];
			}
			t->data[tensor_index_to_offset(t->ndim, t->dim, index_t)] = b->data[j];
		}
		aligned_free(index_t);
		aligned_free(index_b);

		for (int i = 0; i < s->ndim; i++)
		{
			aligned_free(index_map[i]);
		}
		aligned_free(index_map);
	}

	aligned_free(index_block);
}


//________________________________________________________________________________________________________________________
///
/// \brief Convert a dense to an equivalent block-sparse tensor, using the sparsity pattern imposed by the provided quantum numbers.
///
/// Entries in the dense tensor not adhering to the quantum number sparsity pattern are ignored.
///
void dense_to_block_sparse_tensor(const struct dense_tensor* restrict t, const enum tensor_axis_direction* axis_dir, const qnumber** restrict qnums, struct block_sparse_tensor* restrict s)
{
	allocate_block_sparse_tensor(t->ndim, t->dim, axis_dir, qnums, s);

	// for each block with matching quantum numbers...
	const long nblocks = integer_product(s->dim_blocks, s->ndim);
	long* index_block = aligned_calloc(MEM_DATA_ALIGN, s->ndim, sizeof(long));
	for (long k = 0; k < nblocks; k++, next_tensor_index(s->ndim, s->dim_blocks, index_block))
	{
		// probe whether quantum numbers sum to zero
		qnumber qsum = 0;
		for (int i = 0; i < s->ndim; i++)
		{
			qsum += s->axis_dir[i] * s->qnums_blocks[i][index_block[i]];
		}
		if (qsum != 0) {
			continue;
		}

		struct dense_tensor* b = s->blocks[k];
		assert(b != NULL);
		assert(b->ndim == s->ndim);

		// fan-out dense to logical indices
		long** index_map = aligned_calloc(MEM_DATA_ALIGN, s->ndim, sizeof(long*));
		for (int i = 0; i < s->ndim; i++)
		{
			index_map[i] = aligned_calloc(MEM_DATA_ALIGN, b->dim[i], sizeof(long));
			long c = 0;
			for (long j = 0; j < s->dim_logical[i]; j++)
			{
				if (s->qnums_logical[i][j] == s->qnums_blocks[i][index_block[i]])
				{
					index_map[i][c] = j;
					c++;
				}
			}
			assert(c == b->dim[i]);
		}

		// collect dense tensor entries
		long* index_b = aligned_calloc(MEM_DATA_ALIGN, b->ndim, sizeof(long));
		long* index_t = aligned_calloc(MEM_DATA_ALIGN, t->ndim, sizeof(long));
		const long nelem = dense_tensor_num_elements(b);
		for (long j = 0; j < nelem; j++, next_tensor_index(b->ndim, b->dim, index_b))
		{
			for (int i = 0; i < b->ndim; i++)
			{
				index_t[i] = index_map[i][index_b[i]];
			}
			b->data[j] = t->data[tensor_index_to_offset(t->ndim, t->dim, index_t)];
		}
		aligned_free(index_t);
		aligned_free(index_b);

		for (int i = 0; i < s->ndim; i++)
		{
			aligned_free(index_map[i]);
		}
		aligned_free(index_map);
	}

	aligned_free(index_block);
}


//________________________________________________________________________________________________________________________
///
/// \brief Generalized transpose of a tensor 't' such that
/// the i-th axis in the output tensor 'r' is the perm[i]-th axis of the input tensor 't'.
///
/// Memory will be allocated for 'r'.
///
void transpose_block_sparse_tensor(const int* restrict perm, const struct block_sparse_tensor* restrict t, struct block_sparse_tensor* restrict r)
{
	r->ndim = t->ndim;

	if (t->ndim == 0)  // special case
	{
		r->dim_logical = NULL;
		r->dim_blocks  = NULL;

		r->axis_dir = NULL;

		r->qnums_logical = NULL;
		r->qnums_blocks  = NULL;

		// allocate memory for a single block
		r->blocks = aligned_calloc(MEM_DATA_ALIGN, 1, sizeof(struct dense_tensor*));
		r->blocks[0] = aligned_calloc(MEM_DATA_ALIGN, 1, sizeof(struct dense_tensor));
		allocate_dense_tensor(0, NULL, r->blocks[0]);
		// copy single number
		r->blocks[0]->data[0] = t->blocks[0]->data[0];

		return;
	}

	// ensure that 'perm' is a valid permutation
	int* ax_list = aligned_calloc(MEM_DATA_ALIGN, t->ndim, sizeof(int));
	for (int i = 0; i < t->ndim; i++)
	{
		assert(0 <= perm[i] && perm[i] < t->ndim);
		ax_list[perm[i]] = 1;
	}
	for (int i = 0; i < t->ndim; i++)
	{
		assert(ax_list[i] == 1);
	}
	aligned_free(ax_list);

	// dimensions
	r->dim_logical = aligned_alloc(MEM_DATA_ALIGN, t->ndim * sizeof(long));
	r->dim_blocks  = aligned_alloc(MEM_DATA_ALIGN, t->ndim * sizeof(long));
	r->axis_dir    = aligned_alloc(MEM_DATA_ALIGN, t->ndim * sizeof(enum tensor_axis_direction));
	for (int i = 0; i < t->ndim; i++)
	{
		r->dim_logical[i] = t->dim_logical[perm[i]];
		r->dim_blocks [i] = t->dim_blocks [perm[i]];
		r->axis_dir   [i] = t->axis_dir   [perm[i]];
	}

	// logical quantum numbers
	r->qnums_logical = aligned_calloc(MEM_DATA_ALIGN, t->ndim, sizeof(qnumber*));
	for (int i = 0; i < t->ndim; i++)
	{
		r->qnums_logical[i] = aligned_alloc(MEM_DATA_ALIGN, r->dim_logical[i] * sizeof(qnumber));
		memcpy(r->qnums_logical[i], t->qnums_logical[perm[i]], r->dim_logical[i] * sizeof(qnumber));
	}

	// block quantum numbers
	r->qnums_blocks = aligned_calloc(MEM_DATA_ALIGN, t->ndim, sizeof(qnumber*));
	for (int i = 0; i < t->ndim; i++)
	{
		r->qnums_blocks[i] = aligned_alloc(MEM_DATA_ALIGN, r->dim_blocks[i] * sizeof(qnumber));
		memcpy(r->qnums_blocks[i], t->qnums_blocks[perm[i]], r->dim_blocks[i] * sizeof(qnumber));
	}

	// dense tensor blocks
	const long nblocks = integer_product(t->dim_blocks, t->ndim);
	r->blocks = aligned_calloc(MEM_DATA_ALIGN, nblocks, sizeof(struct dense_tensor*));
	long* index_block_t = aligned_calloc(MEM_DATA_ALIGN, t->ndim, sizeof(long));
	long* index_block_r = aligned_calloc(MEM_DATA_ALIGN, r->ndim, sizeof(long));
	for (long k = 0; k < nblocks; k++, next_tensor_index(t->ndim, t->dim_blocks, index_block_t))
	{
		// probe whether quantum numbers sum to zero
		qnumber qsum = 0;
		for (int i = 0; i < t->ndim; i++)
		{
			qsum += t->axis_dir[i] * t->qnums_blocks[i][index_block_t[i]];
		}
		if (qsum != 0) {
			continue;
		}

		assert(t->blocks[k] != NULL);

		// corresponding block index in 'r'
		for (int i = 0; i < t->ndim; i++) {
			index_block_r[i] = index_block_t[perm[i]];
		}
		long j = tensor_index_to_offset(r->ndim, r->dim_blocks, index_block_r);

		// transpose dense tensor block
		r->blocks[j] = aligned_calloc(MEM_DATA_ALIGN, 1, sizeof(struct dense_tensor));
		transpose_dense_tensor(perm, t->blocks[k], r->blocks[j]);
	}
	aligned_free(index_block_r);
	aligned_free(index_block_t);
}


//________________________________________________________________________________________________________________________
///
/// \brief Generalized conjugate transpose of a tensor 't' such that
/// the i-th axis in the output tensor 'r' is the perm[i]-th axis of the input tensor 't'.
///
/// Memory will be allocated for 'r'.
///
void conjugate_transpose_block_sparse_tensor(const int* restrict perm, const struct block_sparse_tensor* restrict t, struct block_sparse_tensor* restrict r)
{
	transpose_block_sparse_tensor(perm, t, r);
	conjugate_block_sparse_tensor(r);
}


//________________________________________________________________________________________________________________________
///
/// \brief Flatten the two neighboring axes (tensor legs) 'i_ax' and 'i_ax + 1' into a single axis.
///
/// Memory will be allocated for 'r'.
///
/// Note: this operation changes the internal dense block structure.
///
void flatten_block_sparse_tensor_axes(const struct block_sparse_tensor* restrict t, const long i_ax, const enum tensor_axis_direction new_axis_dir, struct block_sparse_tensor* restrict r)
{
	assert(0 <= i_ax && i_ax + 1 < t->ndim);

	// construct new block-sparse tensor 'r'
	{
		long* r_dim_logical = aligned_alloc(MEM_DATA_ALIGN, (t->ndim - 1) * sizeof(long));
		enum tensor_axis_direction* r_axis_dir = aligned_alloc(MEM_DATA_ALIGN, (t->ndim - 1) * sizeof(enum tensor_axis_direction));
		for (int i = 0; i < i_ax; i++)
		{
			r_dim_logical[i] = t->dim_logical[i];
			r_axis_dir   [i] = t->axis_dir   [i];
		}
		r_dim_logical[i_ax] = t->dim_logical[i_ax] * t->dim_logical[i_ax + 1];
		r_axis_dir[i_ax]    = new_axis_dir;
		for (int i = i_ax + 2; i < t->ndim; i++)
		{
			r_dim_logical[i - 1] = t->dim_logical[i];
			r_axis_dir   [i - 1] = t->axis_dir   [i];
		}
		// logical quantum numbers
		qnumber* r_qnums_ax_flat = aligned_alloc(MEM_DATA_ALIGN, r_dim_logical[i_ax] * sizeof(qnumber));
		for (long j = 0; j < t->dim_logical[i_ax]; j++)
		{
			for (long k = 0; k < t->dim_logical[i_ax + 1]; k++)
			{
				r_qnums_ax_flat[j*t->dim_logical[i_ax + 1] + k] =
					new_axis_dir * (t->axis_dir[i_ax    ] * t->qnums_logical[i_ax    ][j] +
									t->axis_dir[i_ax + 1] * t->qnums_logical[i_ax + 1][k]);
			}
		}
		qnumber** r_qnums_logical = aligned_alloc(MEM_DATA_ALIGN, (t->ndim - 1) * sizeof(qnumber*));
		for (int i = 0; i < i_ax; i++)
		{
			// simply copy the pointer
			r_qnums_logical[i] = t->qnums_logical[i];
		}
		r_qnums_logical[i_ax] = r_qnums_ax_flat;
		for (int i = i_ax + 2; i < t->ndim; i++)
		{
			// simply copy the pointer
			r_qnums_logical[i - 1] = t->qnums_logical[i];
		}

		// allocate new block-sparse tensor 'r'
		allocate_block_sparse_tensor(t->ndim - 1, r_dim_logical, r_axis_dir, (const qnumber**)r_qnums_logical, r);

		aligned_free(r_qnums_logical);
		aligned_free(r_qnums_ax_flat);
		aligned_free(r_axis_dir);
		aligned_free(r_dim_logical);
	}

	// for each block with matching quantum numbers...
	const long nblocks = integer_product(t->dim_blocks, t->ndim);
	long* index_block_t = aligned_calloc(MEM_DATA_ALIGN, t->ndim, sizeof(long));
	long* index_block_r = aligned_calloc(MEM_DATA_ALIGN, r->ndim, sizeof(long));
	for (long k = 0; k < nblocks; k++, next_tensor_index(t->ndim, t->dim_blocks, index_block_t))
	{
		// probe whether quantum numbers sum to zero
		qnumber qsum = 0;
		for (int i = 0; i < t->ndim; i++)
		{
			qsum += t->axis_dir[i] * t->qnums_blocks[i][index_block_t[i]];
		}
		if (qsum != 0) {
			continue;
		}

		const struct dense_tensor* bt = t->blocks[k];
		assert(bt != NULL);
		assert(bt->ndim == t->ndim);

		const qnumber qnums_i_ax[2] = {
			t->qnums_blocks[i_ax    ][index_block_t[i_ax    ]],
			t->qnums_blocks[i_ax + 1][index_block_t[i_ax + 1]] };

		const qnumber qnum_flat =
			new_axis_dir * (t->axis_dir[i_ax    ] * qnums_i_ax[0] +
			                t->axis_dir[i_ax + 1] * qnums_i_ax[1]);

		// corresponding block index in 'r'
		for (int i = 0; i < i_ax; i++) {
			index_block_r[i] = index_block_t[i];
		}
		index_block_r[i_ax] = -1;
		for (long j = 0; j < r->dim_blocks[i_ax]; j++)
		{
			if (r->qnums_blocks[i_ax][j] == qnum_flat) {
				index_block_r[i_ax] = j;
				break;
			}
		}
		assert(index_block_r[i_ax] != -1);
		for (int i = i_ax + 2; i < t->ndim; i++)
		{
			index_block_r[i - 1] = index_block_t[i];
		}
		struct dense_tensor* br = r->blocks[tensor_index_to_offset(r->ndim, r->dim_blocks, index_block_r)];
		assert(br != NULL);
		assert(br->ndim == r->ndim);

		// construct index map for dense block entries along to-be flattened dimensions
		long* index_map_block = aligned_alloc(MEM_DATA_ALIGN, bt->dim[i_ax] * bt->dim[i_ax + 1] * sizeof(long));
		{
			// fan-out dense to logical indices for original axes 'i_ax' and 'i_ax + 1'
			long* index_map_fanout[2];
			for (int i = 0; i < 2; i++)
			{
				index_map_fanout[i] = aligned_calloc(MEM_DATA_ALIGN, bt->dim[i_ax + i], sizeof(long));
				long c = 0;
				for (long j = 0; j < t->dim_logical[i_ax + i]; j++)
				{
					if (t->qnums_logical[i_ax + i][j] == qnums_i_ax[i])
					{
						index_map_fanout[i][c] = j;
						c++;
					}
				}
				assert(c == bt->dim[i_ax + i]);
			}
			// fan-in logical to block indices for flattened axis
			long* index_map_fanin = aligned_calloc(MEM_DATA_ALIGN, r->dim_logical[i_ax], sizeof(long));
			long c = 0;
			for (long j = 0; j < r->dim_logical[i_ax]; j++)
			{
				if (r->qnums_logical[i_ax][j] == qnum_flat) {
					index_map_fanin[j] = c;
					c++;
				}
			}
			for (long j0 = 0; j0 < bt->dim[i_ax]; j0++)
			{
				for (long j1 = 0; j1 < bt->dim[i_ax + 1]; j1++)
				{
					index_map_block[j0*bt->dim[i_ax + 1] + j1] = index_map_fanin[index_map_fanout[0][j0] * t->dim_logical[i_ax + 1] + index_map_fanout[1][j1]];
				}
			}
			aligned_free(index_map_fanin);
			for (int i = 0; i < 2; i++) {
				aligned_free(index_map_fanout[i]);
			}
		}

		// copy block tensor entries
		const long nslices = integer_product(bt->dim, i_ax + 2);
		long* index_slice_bt = aligned_calloc(MEM_DATA_ALIGN, i_ax + 2, sizeof(long));
		long* index_slice_br = aligned_calloc(MEM_DATA_ALIGN, i_ax + 1, sizeof(long));
		const long stride = integer_product(bt->dim + (i_ax + 2), bt->ndim - (i_ax + 2));
		assert(stride == integer_product(br->dim + (i_ax + 1), br->ndim - (i_ax + 1)));
		for (long j = 0; j < nslices; j++, next_tensor_index(i_ax + 2, bt->dim, index_slice_bt))
		{
			for (int i = 0; i < i_ax; i++) {
				index_slice_br[i] = index_slice_bt[i];
			}
			index_slice_br[i_ax] = index_map_block[index_slice_bt[i_ax]*bt->dim[i_ax + 1] + index_slice_bt[i_ax + 1]];
			const long l = tensor_index_to_offset(i_ax + 1, br->dim, index_slice_br);
			// copy slice of entries
			memcpy(br->data + l*stride, bt->data + j*stride, stride * sizeof(numeric));
		}
		aligned_free(index_slice_br);
		aligned_free(index_slice_bt);

		aligned_free(index_map_block);
	}

	aligned_free(index_block_t);
	aligned_free(index_block_r);
}


//________________________________________________________________________________________________________________________
///
/// \brief Multiply trailing 'ndim_mult' axes in 's' by leading 'ndim_mult' axes in 't', and store result in 'r'.
///
/// Memory will be allocated for 'r'. Operation requires that the quantum numbers of the to-be contracted axes match,
/// and that the axis directions are reversed between the tensors.
///
void block_sparse_tensor_dot(const struct block_sparse_tensor* restrict s, const struct block_sparse_tensor* restrict t, const int ndim_mult, struct block_sparse_tensor* restrict r)
{
	// dimension and quantum number compatibility checks
	assert(ndim_mult >= 1);
	assert(s->ndim >= ndim_mult && t->ndim >= ndim_mult);
	for (int i = 0; i < ndim_mult; i++)
	{
		assert(s->dim_logical[s->ndim - ndim_mult + i] ==  t->dim_logical[i]);
		assert(s->dim_blocks [s->ndim - ndim_mult + i] ==  t->dim_blocks [i]);
		assert(s->axis_dir   [s->ndim - ndim_mult + i] == -t->axis_dir   [i]);
		// quantum numbers must match entrywise
		for (long j = 0; j < t->dim_logical[i]; j++) {
			assert(s->qnums_logical[s->ndim - ndim_mult + i][j] == t->qnums_logical[i][j]);
		}
		for (long j = 0; j < t->dim_blocks[i]; j++) {
			assert(s->qnums_blocks[s->ndim - ndim_mult + i][j] == t->qnums_blocks[i][j]);
		}
	}

	// logical dimensions of new tensor 'r'
	long* r_dim_logical = aligned_alloc(MEM_DATA_ALIGN, (s->ndim + t->ndim - 2*ndim_mult) * sizeof(long));
	for (int i = 0; i < s->ndim - ndim_mult; i++)
	{
		r_dim_logical[i] = s->dim_logical[i];
	}
	for (int i = ndim_mult; i < t->ndim; i++)
	{
		r_dim_logical[s->ndim + i - 2*ndim_mult] = t->dim_logical[i];
	}
	// axis directions of new tensor 'r'
	enum tensor_axis_direction* r_axis_dir = aligned_alloc(MEM_DATA_ALIGN, (s->ndim + t->ndim - 2*ndim_mult) * sizeof(enum tensor_axis_direction));
	for (int i = 0; i < s->ndim - ndim_mult; i++)
	{
		r_axis_dir[i] = s->axis_dir[i];
	}
	for (int i = ndim_mult; i < t->ndim; i++)
	{
		r_axis_dir[s->ndim + i - 2*ndim_mult] = t->axis_dir[i];
	}
	// logical quantum numbers along each dimension
	const qnumber** r_qnums_logical = aligned_alloc(MEM_DATA_ALIGN, (s->ndim + t->ndim - 2*ndim_mult) * sizeof(qnumber*));
	for (int i = 0; i < s->ndim - ndim_mult; i++)
	{
		// simply copy the pointer
		r_qnums_logical[i] = s->qnums_logical[i];
	}
	for (int i = ndim_mult; i < t->ndim; i++)
	{
		// simply copy the pointer
		r_qnums_logical[s->ndim + i - 2*ndim_mult] = t->qnums_logical[i];
	}
	// create new tensor 'r'
	allocate_block_sparse_tensor(s->ndim + t->ndim - 2*ndim_mult, r_dim_logical, r_axis_dir, r_qnums_logical, r);
	aligned_free(r_qnums_logical);
	aligned_free(r_axis_dir);
	aligned_free(r_dim_logical);

	// for each dense block of 'r'...
	const long nblocks = integer_product(r->dim_blocks, r->ndim);
	long* index_block_s = aligned_calloc(MEM_DATA_ALIGN, s->ndim, sizeof(long));
	long* index_block_t = aligned_calloc(MEM_DATA_ALIGN, t->ndim, sizeof(long));
	long* index_block_r = aligned_calloc(MEM_DATA_ALIGN, r->ndim, sizeof(long));
	for (long k = 0; k < nblocks; k++, next_tensor_index(r->ndim, r->dim_blocks, index_block_r))
	{
		// probe whether quantum numbers in 'r' sum to zero
		qnumber qsum = 0;
		for (int i = 0; i < r->ndim; i++)
		{
			qsum += r->axis_dir[i] * r->qnums_blocks[i][index_block_r[i]];
		}
		if (qsum != 0) {
			continue;
		}

		struct dense_tensor* br = r->blocks[k];
		assert(br != NULL);
		assert(br->ndim == r->ndim);

		// for each quantum number combination of the to-be contracted axes...
		const long ncontract = integer_product(t->dim_blocks, ndim_mult);
		long* index_contract = aligned_calloc(MEM_DATA_ALIGN, ndim_mult, sizeof(long));
		for (long m = 0; m < ncontract; m++, next_tensor_index(ndim_mult, t->dim_blocks, index_contract))
		{
			for (int i = 0; i < s->ndim; i++) {
				index_block_s[i] = (i < s->ndim - ndim_mult) ? index_block_r[i] : index_contract[i - (s->ndim - ndim_mult)];
			}
			// probe whether quantum numbers in 's' sum to zero
			qnumber qsum = 0;
			for (int i = 0; i < s->ndim; i++)
			{
				qsum += s->axis_dir[i] * s->qnums_blocks[i][index_block_s[i]];
			}
			if (qsum != 0) {
				continue;
			}

			for (int i = 0; i < t->ndim; i++) {
				index_block_t[i] = (i < ndim_mult) ? index_contract[i] : index_block_r[(s->ndim - ndim_mult) + (i - ndim_mult)];
			}
			// quantum numbers in 't' must now also sum to zero
			qsum = 0;
			for (int i = 0; i < t->ndim; i++)
			{
				qsum += t->axis_dir[i] * t->qnums_blocks[i][index_block_t[i]];
			}
			assert(qsum == 0);

			const struct dense_tensor* bs = s->blocks[tensor_index_to_offset(s->ndim, s->dim_blocks, index_block_s)];
			const struct dense_tensor* bt = t->blocks[tensor_index_to_offset(t->ndim, t->dim_blocks, index_block_t)];
			assert(bs != NULL);
			assert(bt != NULL);

			// actually multiply dense tensor blocks and add result to 'br'
			dense_tensor_dot_update(1.0, bs, bt, ndim_mult, br, 1.0);
		}

		aligned_free(index_contract);
	}

	aligned_free(index_block_r);
	aligned_free(index_block_t);
	aligned_free(index_block_s);
}
