#include <math.h>
#include <complex.h>
#include "dense_tensor.h"
#include "aligned_memory.h"


char* test_dense_tensor_trace()
{
	hid_t file = H5Fopen("../test/data/test_dense_tensor_trace.hdf5", H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file < 0) {
		return "'H5Fopen' in test_dense_tensor_trace failed";
	}

	struct dense_tensor t;
	const long tdim[3] = { 5, 5, 5 };
	allocate_dense_tensor(DOUBLE_COMPLEX, 3, tdim, &t);
	// read values from disk
	if (read_hdf5_dataset(file, "t", H5T_NATIVE_DOUBLE, t.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	dcomplex tr;
	dense_tensor_trace(&t, &tr);

	// reference value for checking
	dcomplex tr_ref;
	if (read_hdf5_dataset(file, "tr", H5T_NATIVE_DOUBLE, &tr_ref) < 0) {
		return "reading trace value from disk failed";
	}

	// compare
	if (cabs(tr - tr_ref) > 1e-13) {
		return "tensor trace does not match reference";
	}

	// clean up
	delete_dense_tensor(&t);

	H5Fclose(file);

	return 0;
}


char* test_dense_tensor_transpose()
{
	hid_t file = H5Fopen("../test/data/test_dense_tensor_transpose.hdf5", H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file < 0) {
		return "'H5Fopen' in test_dense_tensor_transpose failed";
	}

	const int ndim = 10;

	// create tensor 't'
	struct dense_tensor t;
	const long dim[10] = { 1, 4, 5, 1, 1, 2, 1, 3, 1, 7 };
	allocate_dense_tensor(SINGLE_REAL, ndim, dim,  &t);
	// read values from disk
	if (read_hdf5_dataset(file, "t", H5T_NATIVE_FLOAT, t.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	// generalized transposition
	const int perm[10] = { 4, 8, 2, 6, 0, 9, 5, 7, 3, 1 };
	struct dense_tensor t_tp;
	transpose_dense_tensor(perm, &t, &t_tp);

	// reference tensor
	const long refdim[10] = { 1, 1, 5, 1, 1, 7, 2, 3, 1, 4 };
	struct dense_tensor t_tp_ref;
	allocate_dense_tensor(SINGLE_REAL, ndim, refdim, &t_tp_ref);
	// read values from disk
	if (read_hdf5_dataset(file, "t_tp", H5T_NATIVE_FLOAT, t_tp_ref.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	// compare
	if (!dense_tensor_allclose(&t_tp, &t_tp_ref, 0.)) {
		return "transposed tensor does not match reference";
	}

	// clean up
	delete_dense_tensor(&t_tp_ref);
	delete_dense_tensor(&t_tp);
	delete_dense_tensor(&t);

	H5Fclose(file);

	return 0;
}


char* test_dense_tensor_slice()
{
	hid_t file = H5Fopen("../test/data/test_dense_tensor_slice.hdf5", H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file < 0) {
		return "'H5Fopen' in test_dense_tensor_slice failed";
	}

	// create tensor 't'
	struct dense_tensor t;
	const long dim[5] = { 2, 7, 3, 5, 4 };
	allocate_dense_tensor(SINGLE_COMPLEX, 5, dim, &t);
	// read values from disk
	if (read_hdf5_dataset(file, "t", H5T_NATIVE_FLOAT, t.data) < 0) {
		return "reading tensor entries from disk failed";
	}
	// read indices from disk
	const long nind = 10;
	long *ind = aligned_alloc(MEM_DATA_ALIGN, nind * sizeof(long));
	if (read_hdf5_attribute(file, "ind", H5T_NATIVE_LONG, ind) < 0) {
		return "reading slice indices from disk failed";
	}

	struct dense_tensor s;
	dense_tensor_slice(&t, 1, ind, nind, &s);

	// read reference tensor from disk
	struct dense_tensor s_ref;
	const long dim_ref[5] = { 2, 10, 3, 5, 4 };
	allocate_dense_tensor(SINGLE_COMPLEX, 5, dim_ref, &s_ref);
	// read values from disk
	if (read_hdf5_dataset(file, "s", H5T_NATIVE_FLOAT, s_ref.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	// compare
	if (!dense_tensor_allclose(&s, &s_ref, 0.)) {
		return "sliced tensor does not match reference";
	}

	// clean up
	delete_dense_tensor(&s_ref);
	delete_dense_tensor(&s);
	aligned_free(ind);
	delete_dense_tensor(&t);

	H5Fclose(file);

	return 0;
}


char* test_dense_tensor_multiply_pointwise()
{
	hid_t file = H5Fopen("../test/data/test_dense_tensor_multiply_pointwise.hdf5", H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file < 0) {
		return "'H5Fopen' in test_dense_tensor_multiply_pointwise failed";
	}

	// create tensor 't'
	struct dense_tensor t;
	const long tdim[3] = { 2, 6, 5 };
	allocate_dense_tensor(SINGLE_COMPLEX, 3, tdim,  &t);
	// read values from disk
	if (read_hdf5_dataset(file, "t", H5T_NATIVE_FLOAT, t.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	for (int i = 0; i < 2; i++)
	{
		// create another tensor 's'
		struct dense_tensor s;
		allocate_dense_tensor(SINGLE_REAL, 2, &tdim[i], &s);
		// read values from disk
		char varname[1024];
		sprintf(varname, "s%i", i);
		if (read_hdf5_dataset(file, varname, H5T_NATIVE_FLOAT, s.data) < 0) {
			return "reading tensor entries from disk failed";
		}

		// multiply tensors pointwise
		struct dense_tensor t_mult_s;
		dense_tensor_multiply_pointwise(&t, &s, i == 0 ? TENSOR_AXIS_RANGE_LEADING : TENSOR_AXIS_RANGE_TRAILING, &t_mult_s);

		// reference tensors for checking
		const long refdim[3] = { 2, 6, 5 };
		struct dense_tensor t_mult_s_ref;
		allocate_dense_tensor(SINGLE_COMPLEX, 3, refdim, &t_mult_s_ref);
		// read values from disk
		sprintf(varname, "t_mult_s%i", i);
		if (read_hdf5_dataset(file, varname, H5T_NATIVE_FLOAT, t_mult_s_ref.data) < 0) {
			return "reading tensor entries from disk failed";
		}

		// compare
		if (!dense_tensor_allclose(&t_mult_s, &t_mult_s_ref, 1e-13)) {
			return "pointwise product of tensors does not match reference";
		}

		delete_dense_tensor(&t_mult_s_ref);
		delete_dense_tensor(&t_mult_s);
		delete_dense_tensor(&s);
	}

	delete_dense_tensor(&t);

	H5Fclose(file);

	return 0;
}


char* test_dense_tensor_dot()
{
	hid_t file = H5Fopen("../test/data/test_dense_tensor_dot.hdf5", H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file < 0) {
		return "'H5Fopen' in test_dense_tensor_dot failed";
	}

	// create tensor 't'
	struct dense_tensor t;
	const long tdim[5] = { 2, 11, 3, 4, 5 };
	allocate_dense_tensor(DOUBLE_COMPLEX, 5, tdim,  &t);
	// read values from disk
	if (read_hdf5_dataset(file, "t", H5T_NATIVE_DOUBLE, t.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	// create another tensor 's'
	struct dense_tensor s;
	const long sdim[4] = { 4, 5, 7, 6 };
	allocate_dense_tensor(DOUBLE_COMPLEX, 4, sdim, &s);
	// read values from disk
	if (read_hdf5_dataset(file, "s", H5T_NATIVE_DOUBLE, s.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	// reference tensor for checking
	const long refdim[5] = { 2, 11, 3, 7, 6 };
	struct dense_tensor t_dot_s_ref;
	allocate_dense_tensor(DOUBLE_COMPLEX, 5, refdim, &t_dot_s_ref);
	// read values from disk
	if (read_hdf5_dataset(file, "t_dot_s", H5T_NATIVE_DOUBLE, t_dot_s_ref.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	for (enum tensor_axis_range axrange_t = 0; axrange_t < TENSOR_AXIS_RANGE_NUM; axrange_t++)
	{
		struct dense_tensor tp;
		if (axrange_t == TENSOR_AXIS_RANGE_TRAILING) {
			copy_dense_tensor(&t, &tp);
		}
		else {
			const int perm[5] = { 3, 4, 0, 1, 2 };
			transpose_dense_tensor(perm, &t, &tp);
		}

		for (enum tensor_axis_range axrange_s = 0; axrange_s < TENSOR_AXIS_RANGE_NUM; axrange_s++)
		{
			struct dense_tensor sp;
			if (axrange_s == TENSOR_AXIS_RANGE_LEADING) {
				copy_dense_tensor(&s, &sp);
			}
			else {
				const int perm[4] = { 2, 3, 0, 1 };
				transpose_dense_tensor(perm, &s, &sp);
			}

			// multiply tensors and store result in 't_dot_s'
			struct dense_tensor t_dot_s;
			dense_tensor_dot(&tp, axrange_t, &sp, axrange_s, 2, &t_dot_s);

			// compare
			if (!dense_tensor_allclose(&t_dot_s, &t_dot_s_ref, 1e-13)) {
				return "dot product of tensors does not match reference";
			}

			delete_dense_tensor(&t_dot_s);

			delete_dense_tensor(&sp);
		}

		delete_dense_tensor(&tp);
	}

	// clean up
	delete_dense_tensor(&t_dot_s_ref);
	delete_dense_tensor(&s);
	delete_dense_tensor(&t);

	H5Fclose(file);

	return 0;
}


char* test_dense_tensor_dot_update()
{
	hid_t file = H5Fopen("../test/data/test_dense_tensor_dot_update.hdf5", H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file < 0) {
		return "'H5Fopen' in test_dense_tensor_dot_update failed";
	}

	const scomplex alpha =  1.2f - 0.3f*I;
	const scomplex beta  = -0.7f + 0.8f*I;

	// create tensor 't'
	struct dense_tensor t;
	const long dim[5] = { 2, 11, 3, 4, 5 };
	allocate_dense_tensor(SINGLE_COMPLEX, 5, dim, &t);
	// read values from disk
	if (read_hdf5_dataset(file, "t", H5T_NATIVE_FLOAT, t.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	// create another tensor 's'
	struct dense_tensor s;
	const long sdim[4] = { 4, 5, 7, 6 };
	allocate_dense_tensor(SINGLE_COMPLEX, 4, sdim, &s);
	// read values from disk
	if (read_hdf5_dataset(file, "s", H5T_NATIVE_FLOAT, s.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	// reference tensor for checking
	const long refdim[5] = { 2, 11, 3, 7, 6 };
	struct dense_tensor t_dot_s_ref;
	allocate_dense_tensor(SINGLE_COMPLEX, 5, refdim, &t_dot_s_ref);
	// read values from disk
	if (read_hdf5_dataset(file, "t_dot_s_1", H5T_NATIVE_FLOAT, t_dot_s_ref.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	for (enum tensor_axis_range axrange_t = 0; axrange_t < TENSOR_AXIS_RANGE_NUM; axrange_t++)
	{
		struct dense_tensor tp;
		if (axrange_t == TENSOR_AXIS_RANGE_TRAILING) {
			copy_dense_tensor(&t, &tp);
		}
		else {
			const int perm[5] = { 3, 4, 0, 1, 2 };
			transpose_dense_tensor(perm, &t, &tp);
		}

		for (enum tensor_axis_range axrange_s = 0; axrange_s < TENSOR_AXIS_RANGE_NUM; axrange_s++)
		{
			struct dense_tensor sp;
			if (axrange_s == TENSOR_AXIS_RANGE_LEADING) {
				copy_dense_tensor(&s, &sp);
			}
			else {
				const int perm[4] = { 2, 3, 0, 1 };
				transpose_dense_tensor(perm, &s, &sp);
			}

			// multiply tensors and update 't_dot_s' with result
			struct dense_tensor t_dot_s;
			const long t_dot_s_dim[5] = { 2, 11, 3, 7, 6 };
			allocate_dense_tensor(SINGLE_COMPLEX, 5, t_dot_s_dim, &t_dot_s);
			// read values from disk
			if (read_hdf5_dataset(file, "t_dot_s_0", H5T_NATIVE_FLOAT, t_dot_s.data) < 0) {
				return "reading tensor entries from disk failed";
			}
			dense_tensor_dot_update(&alpha, &tp, axrange_t, &sp, axrange_s, 2, &t_dot_s, &beta);

			// compare
			if (!dense_tensor_allclose(&t_dot_s, &t_dot_s_ref, 1e-5)) {
				return "tensor updated by dot product of two other tensors does not match reference";
			}

			delete_dense_tensor(&t_dot_s);

			delete_dense_tensor(&sp);
		}

		delete_dense_tensor(&tp);
	}

	// clean up
	delete_dense_tensor(&t_dot_s_ref);
	delete_dense_tensor(&s);
	delete_dense_tensor(&t);

	H5Fclose(file);

	return 0;
}


char* test_dense_tensor_kronecker_product()
{
	hid_t file = H5Fopen("../test/data/test_dense_tensor_kronecker_product.hdf5", H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file < 0) {
		return "'H5Fopen' in test_dense_tensor_kronecker_product failed";
	}

	// create tensor 's'
	struct dense_tensor s;
	const long sdim[4] = { 6, 5, 7, 2 };
	allocate_dense_tensor(DOUBLE_COMPLEX, 4, sdim, &s);
	// read values from disk
	if (read_hdf5_dataset(file, "s", H5T_NATIVE_DOUBLE, s.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	// create tensor 't'
	struct dense_tensor t;
	const long tdim[4] = { 3, 11, 2, 5 };
	allocate_dense_tensor(DOUBLE_COMPLEX, 4, tdim,  &t);
	// read values from disk
	if (read_hdf5_dataset(file, "t", H5T_NATIVE_DOUBLE, t.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	struct dense_tensor r;
	dense_tensor_kronecker_product(&s, &t, &r);

	// load reference values from disk
	struct dense_tensor r_ref;
	const long refdim[4] = { 18, 55, 14, 10 };
	allocate_dense_tensor(DOUBLE_COMPLEX, 4, refdim, &r_ref);
	// read values from disk
	if (read_hdf5_dataset(file, "r", H5T_NATIVE_DOUBLE, r_ref.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	// compare
	if (!dense_tensor_allclose(&r, &r_ref, 1e-13)) {
		return "Kronecker product of tensors does not match reference";
	}

	// clean up
	delete_dense_tensor(&r_ref);
	delete_dense_tensor(&r);
	delete_dense_tensor(&t);
	delete_dense_tensor(&s);

	H5Fclose(file);

	return 0;
}


char* test_dense_tensor_kronecker_product_degree_zero()
{
	hid_t file = H5Fopen("../test/data/test_dense_tensor_kronecker_product_degree_zero.hdf5", H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file < 0) {
		return "'H5Fopen' in test_dense_tensor_kronecker_product_degree_zero failed";
	}

	// create tensor 's'
	struct dense_tensor s;
	allocate_dense_tensor(SINGLE_COMPLEX, 0, NULL, &s);
	// read values from disk
	if (read_hdf5_dataset(file, "s", H5T_NATIVE_FLOAT, s.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	// create tensor 't'
	struct dense_tensor t;
	allocate_dense_tensor(SINGLE_COMPLEX, 0, NULL,  &t);
	// read values from disk
	if (read_hdf5_dataset(file, "t", H5T_NATIVE_FLOAT, t.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	struct dense_tensor r;
	dense_tensor_kronecker_product(&s, &t, &r);

	// load reference values from disk
	struct dense_tensor r_ref;
	allocate_dense_tensor(SINGLE_COMPLEX, 0, NULL, &r_ref);
	// read values from disk
	if (read_hdf5_dataset(file, "r", H5T_NATIVE_FLOAT, r_ref.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	// compare
	if (!dense_tensor_allclose(&r, &r_ref, 1e-5)) {
		return "Kronecker product of tensors does not match reference";
	}

	// clean up
	delete_dense_tensor(&r_ref);
	delete_dense_tensor(&r);
	delete_dense_tensor(&t);
	delete_dense_tensor(&s);

	H5Fclose(file);

	return 0;
}


char* test_dense_tensor_qr()
{
	hid_t file = H5Fopen("../test/data/test_dense_tensor_qr.hdf5", H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file < 0) {
		return "'H5Fopen' in test_dense_tensor_qr failed";
	}

	const enum numeric_type dtypes[4] = { SINGLE_REAL, DOUBLE_REAL, SINGLE_COMPLEX, DOUBLE_COMPLEX };

	// cases m >= n and m < n
	for (int i = 0; i < 2; i++)
	{
		// data types
		for (int j = 0; j < 4; j++)
		{
			const double tol = (j % 2 == 0 ? 1e-6 : 1e-13);

			// matrix 'a'
			struct dense_tensor a;
			const long dim[2] = { i == 0 ? 11 : 5, i == 0 ? 7 : 13 };
			allocate_dense_tensor(dtypes[j], 2, dim, &a);
			// read values from disk
			char varname[1024];
			sprintf(varname, "a_s%i_t%i", i, j);
			if (read_hdf5_dataset(file, varname, j % 2 == 0 ? H5T_NATIVE_FLOAT : H5T_NATIVE_DOUBLE, a.data) < 0) {
				return "reading tensor entries from disk failed";
			}

			// perform QR decomposition
			struct dense_tensor q, r;
			dense_tensor_qr(&a, &q, &r);

			// matrix product 'q r' must be equal to 'a'
			struct dense_tensor qr;
			dense_tensor_dot(&q, TENSOR_AXIS_RANGE_TRAILING, &r, TENSOR_AXIS_RANGE_LEADING, 1, &qr);
			if (!dense_tensor_allclose(&qr, &a, tol)) {
				return "matrix product Q R is not equal to original A matrix";
			}
			delete_dense_tensor(&qr);

			// 'q' must be an isometry
			struct dense_tensor qc;
			copy_dense_tensor(&q, &qc);
			conjugate_dense_tensor(&qc);
			struct dense_tensor qhq;
			dense_tensor_dot(&qc, TENSOR_AXIS_RANGE_LEADING, &q, TENSOR_AXIS_RANGE_LEADING, 1, &qhq);
			if (!dense_tensor_is_identity(&qhq, tol)) {
				return "Q matrix is not an isometry";
			}
			delete_dense_tensor(&qhq);
			delete_dense_tensor(&qc);

			// 'r' must be upper triangular
			const long k = dim[0] <= dim[1] ? dim[0] : dim[1];
			void* zero_vec = aligned_calloc(MEM_DATA_ALIGN, k, sizeof_numeric_type(r.dtype));
			for (long l = 0; l < k; l++) {
				if (uniform_distance(r.dtype, l, (char*)r.data + (l*dim[1])*sizeof_numeric_type(r.dtype), zero_vec) != 0) {
					return "R matrix is not upper triangular";
				}
			}
			aligned_free(zero_vec);

			delete_dense_tensor(&r);
			delete_dense_tensor(&q);
			delete_dense_tensor(&a);
		}
	}

	H5Fclose(file);

	return 0;
}


char* test_dense_tensor_rq()
{
	hid_t file = H5Fopen("../test/data/test_dense_tensor_rq.hdf5", H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file < 0) {
		return "'H5Fopen' in test_dense_tensor_rq failed";
	}

	const enum numeric_type dtypes[4] = { SINGLE_REAL, DOUBLE_REAL, SINGLE_COMPLEX, DOUBLE_COMPLEX };

	// cases m >= n and m < n
	for (int i = 0; i < 2; i++)
	{
		// data types
		for (int j = 0; j < 4; j++)
		{
			const double tol = (j % 2 == 0 ? 1e-6 : 1e-13);

			// matrix 'a'
			struct dense_tensor a;
			const long dim[2] = { i == 0 ? 11 : 5, i == 0 ? 7 : 13 };
			allocate_dense_tensor(dtypes[j], 2, dim, &a);
			// read values from disk
			char varname[1024];
			sprintf(varname, "a_s%i_t%i", i, j);
			if (read_hdf5_dataset(file, varname, j % 2 == 0 ? H5T_NATIVE_FLOAT : H5T_NATIVE_DOUBLE, a.data) < 0) {
				return "reading tensor entries from disk failed";
			}

			// perform RQ decomposition
			struct dense_tensor r, q;
			dense_tensor_rq(&a, &r, &q);

			// matrix product 'r q' must be equal to 'a'
			struct dense_tensor rq;
			dense_tensor_dot(&r, TENSOR_AXIS_RANGE_TRAILING, &q, TENSOR_AXIS_RANGE_LEADING, 1, &rq);
			if (!dense_tensor_allclose(&rq, &a, tol)) {
				return "matrix product R Q is not equal to original A matrix";
			}
			delete_dense_tensor(&rq);

			// 'q' must be an isometry
			struct dense_tensor qc;
			copy_dense_tensor(&q, &qc);
			conjugate_dense_tensor(&qc);
			struct dense_tensor qqh;
			dense_tensor_dot(&q, TENSOR_AXIS_RANGE_TRAILING, &qc, TENSOR_AXIS_RANGE_TRAILING, 1, &qqh);
			if (!dense_tensor_is_identity(&qqh, tol)) {
				return "Q matrix is not an isometry";
			}
			delete_dense_tensor(&qqh);
			delete_dense_tensor(&qc);

			// 'r' must be upper triangular (referenced from the bottom right entry)
			const long k = dim[0] <= dim[1] ? dim[0] : dim[1];
			void* zero_vec = aligned_calloc(MEM_DATA_ALIGN, k, sizeof_numeric_type(r.dtype));
			for (long l = 0; l < k; l++) {
				if (uniform_distance(r.dtype, l, (char*)r.data + ((dim[0] - k + l)*k)*sizeof_numeric_type(r.dtype), zero_vec) != 0) {
					return "R matrix is not upper triangular";
				}
			}
			aligned_free(zero_vec);

			delete_dense_tensor(&r);
			delete_dense_tensor(&q);
			delete_dense_tensor(&a);
		}
	}

	H5Fclose(file);

	return 0;
}


char* test_dense_tensor_svd()
{
	hid_t file = H5Fopen("../test/data/test_dense_tensor_svd.hdf5", H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file < 0) {
		return "'H5Fopen' in test_dense_tensor_svd failed";
	}

	const enum numeric_type dtypes[4] = { SINGLE_REAL, DOUBLE_REAL, SINGLE_COMPLEX, DOUBLE_COMPLEX };

	// cases m >= n and m < n
	for (int i = 0; i < 2; i++)
	{
		// data types
		for (int j = 0; j < 4; j++)
		{
			const double tol = (j % 2 == 0 ? 5e-6 : 1e-13);

			// matrix 'a'
			struct dense_tensor a;
			const long dim[2] = { i == 0 ? 11 : 5, i == 0 ? 7 : 13 };
			allocate_dense_tensor(dtypes[j], 2, dim, &a);
			// read values from disk
			char varname[1024];
			sprintf(varname, "a_s%i_t%i", i, j);
			if (read_hdf5_dataset(file, varname, j % 2 == 0 ? H5T_NATIVE_FLOAT : H5T_NATIVE_DOUBLE, a.data) < 0) {
				return "reading tensor entries from disk failed";
			}

			// compute singular value decomposition
			struct dense_tensor u, s, vh;
			dense_tensor_svd(&a, &u, &s, &vh);

			// matrix product 'u s vh' must be equal to 'a'
			struct dense_tensor us;
			dense_tensor_multiply_pointwise(&u, &s, TENSOR_AXIS_RANGE_TRAILING, &us);
			struct dense_tensor usvh;
			dense_tensor_dot(&us, TENSOR_AXIS_RANGE_TRAILING, &vh, TENSOR_AXIS_RANGE_LEADING, 1, &usvh);
			delete_dense_tensor(&us);
			if (!dense_tensor_allclose(&usvh, &a, tol)) {
				return "matrix product U S V^dag is not equal to original A matrix";
			}
			delete_dense_tensor(&usvh);

			// 'u' must be an isometry
			struct dense_tensor uc;
			copy_dense_tensor(&u, &uc);
			conjugate_dense_tensor(&uc);
			struct dense_tensor uhu;
			dense_tensor_dot(&uc, TENSOR_AXIS_RANGE_LEADING, &u, TENSOR_AXIS_RANGE_LEADING, 1, &uhu);
			if (!dense_tensor_is_identity(&uhu, tol)) {
				return "U matrix is not an isometry";
			}
			delete_dense_tensor(&uhu);
			delete_dense_tensor(&uc);

			// 'v' must be an isometry
			struct dense_tensor vt;
			copy_dense_tensor(&vh, &vt);
			conjugate_dense_tensor(&vt);
			struct dense_tensor vhv;
			dense_tensor_dot(&vh, TENSOR_AXIS_RANGE_TRAILING, &vt, TENSOR_AXIS_RANGE_TRAILING, 1, &vhv);
			if (!dense_tensor_is_identity(&vhv, tol)) {
				return "V matrix is not an isometry";
			}
			delete_dense_tensor(&vhv);
			delete_dense_tensor(&vt);

			delete_dense_tensor(&vh);
			delete_dense_tensor(&s);
			delete_dense_tensor(&u);
			delete_dense_tensor(&a);
		}
	}

	H5Fclose(file);

	return 0;
}


char* test_dense_tensor_block()
{
	hid_t file = H5Fopen("../test/data/test_dense_tensor_block.hdf5", H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file < 0) {
		return "'H5Fopen' in test_dense_tensor_transpose failed";
	}

	// create tensor 't'
	struct dense_tensor t;
	const long dim[4] = { 2, 3, 4, 5 };
	allocate_dense_tensor(DOUBLE_COMPLEX, 4, dim,  &t);
	// read values from disk
	if (read_hdf5_dataset(file, "t", H5T_NATIVE_DOUBLE, t.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	const long bdim[4] = { 1, 2, 4, 3 };

	// indices along each dimension
	const long idx0[1] = { 1 };
	const long idx1[2] = { 0, 2 };
	const long idx2[4] = { 0, 1, 2, 3 };
	const long idx3[3] = { 1, 4, 4 }; // index 4 appears twice
	const long *idx[4] = { idx0, idx1, idx2, idx3 };

	struct dense_tensor b;
	dense_tensor_block(&t, bdim, idx, &b);

	// reference tensor for checking
	struct dense_tensor b_ref;
	allocate_dense_tensor(DOUBLE_COMPLEX, 4, bdim, &b_ref);
	// read values from disk
	if (read_hdf5_dataset(file, "b", H5T_NATIVE_DOUBLE, b_ref.data) < 0) {
		return "reading tensor entries from disk failed";
	}

	// compare
	if (!dense_tensor_allclose(&b, &b_ref, 1e-15)) {
		return "extracted sub-block does not match reference";
	}

	// clean up
	delete_dense_tensor(&b_ref);
	delete_dense_tensor(&b);
	delete_dense_tensor(&t);

	H5Fclose(file);

	return 0;
}
