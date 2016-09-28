/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos
 *
 * tests/suite/io.c
 */

#include "daos_test.h"

#define UPDATE_CSUM_SIZE	32

struct ioreq {
	daos_handle_t	 oh;

	test_arg_t	*arg;

	daos_event_t	 ev;

	daos_dkey_t	 dkey;

	daos_iov_t	 val_iov;

	daos_csum_buf_t	 csum;
	char		 csum_buf[UPDATE_CSUM_SIZE];

	daos_vec_iod_t	 vio;
	daos_recx_t	 rex;
	daos_epoch_range_t erange;

	daos_sg_list_t	 sgl;
};

static void
ioreq_init(struct ioreq *req, daos_obj_id_t oid, test_arg_t *arg)
{
	int rc;

	memset(req, 0, sizeof(*req));

	req->arg = arg;
	if (arg->async) {
		rc = daos_event_init(&req->ev, arg->eq, NULL);
		assert_int_equal(rc, 0);
	}

	/** sgl */
	req->sgl.sg_nr.num = 1;
	req->sgl.sg_iovs = &req->val_iov;

	/** csum */
	daos_csum_set(&req->csum, &req->csum_buf[0], UPDATE_CSUM_SIZE);

	/** record extent */
	req->rex.rx_nr		= 1;
	req->rex.rx_idx		= 0;

	/** epoch range: required by the wire format */
	req->erange.epr_lo = 0;
	req->erange.epr_hi = DAOS_EPOCH_MAX;

	req->vio.vd_kcsum.cs_csum = NULL;
	req->vio.vd_kcsum.cs_buf_len = 0;
	req->vio.vd_kcsum.cs_len = 0;

	/** vector I/O descriptor */
	req->vio.vd_recxs	= &req->rex;
	req->vio.vd_nr		= 1;

	print_message("open oid=%lu.%lu.%lu\n", oid.lo, oid.mid, oid.hi);

	/** open the object */
	rc = dsr_obj_open(arg->coh, oid, 0, 0, &req->oh, NULL);
	assert_int_equal(rc, 0);
}

static void
ioreq_fini(struct ioreq *req)
{
	int rc;

	rc = dsr_obj_close(req->oh, NULL);
	assert_int_equal(rc, 0);

	if (req->arg->async) {
		rc = daos_event_fini(&req->ev);
		assert_int_equal(rc, 0);
	}
}

static void
insert(const char *dkey, const char *akey, uint64_t idx, void *val,
       daos_size_t size, daos_epoch_t epoch, struct ioreq *req)
{
	int		 rc;

	/** dkey */
	daos_iov_set(&req->dkey, (void *)dkey, strlen(dkey));

	/** akey */
	daos_iov_set(&req->vio.vd_name, (void *)akey, strlen(akey));

	/** val */
	daos_iov_set(&req->val_iov, val, size);

	/** record extent */
	req->rex.rx_rsize = size;
	req->rex.rx_idx = idx;

	/** XXX: to be fixed */
	req->erange.epr_lo = epoch;

	/** execute update operation */
	rc = dsr_obj_update(req->oh, epoch, &req->dkey, 1, &req->vio, &req->sgl,
			    req->arg->async ? &req->ev : NULL);
	if (!req->arg->async && rc != 0)
		ioreq_fini(req);
	assert_int_equal(rc, 0);

	if (req->arg->async) {
		daos_event_t	*evp;

		/** wait for update completion */
		rc = daos_eq_poll(req->arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &req->ev);
		if (evp->ev_error != 0)
			ioreq_fini(req);
		assert_int_equal(evp->ev_error, 0);
	}
}

static void
punch(const char *dkey, const char *akey, uint64_t idx,
      daos_epoch_t epoch, struct ioreq *req)
{
	insert(dkey, akey, idx, NULL, 0, epoch, req);
}

static void
lookup(const char *dkey, const char *akey, uint64_t idx, void *val,
       daos_size_t size, daos_epoch_t epoch, struct ioreq *req)
{
	int rc;

	/** dkey */
	daos_iov_set(&req->dkey, (void *)dkey, strlen(dkey));

	/** akey */
	daos_iov_set(&req->vio.vd_name, (void *)akey, strlen(akey));

	/** val */
	daos_iov_set(&req->val_iov, val, size);

	/** record extent */
	req->rex.rx_rsize = DAOS_REC_ANY;
	req->rex.rx_idx = idx;

	/** XXX: to be fixed */
	req->erange.epr_lo = epoch;

	/** execute fetch operation */
	rc = dsr_obj_fetch(req->oh, epoch, &req->dkey, 1, &req->vio, &req->sgl,
			   NULL, req->arg->async ? &req->ev : NULL);
	if (!req->arg->async && rc != 0)
		ioreq_fini(req);
	assert_int_equal(rc, 0);

	if (req->arg->async) {
		daos_event_t	*evp;

		/** wait for fetch completion */
		rc = daos_eq_poll(req->arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &req->ev);
		if (evp->ev_error != 0)
			ioreq_fini(req);
		assert_int_equal(evp->ev_error, 0);
	}
}

static inline void
obj_random(test_arg_t *arg, daos_obj_id_t *oid)
{
	/** choose random object */
	oid->lo	= rand();
	oid->mid = rand();
	oid->hi	= rand();
	dsr_obj_id_generate(oid, DSR_OC_REPLICA_RW);
}

/** test overwrite in different epoch */
static void
io_epoch_overwrite(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_size_t	 size;
	char		 ubuf[] = "DAOS";
	char		 fbuf[] = "DAOS";
	int		 i;
	daos_epoch_t	 e = 0;

	/** choose random object */
	obj_random(arg, &oid);

	ioreq_init(&req, oid, (test_arg_t *)*state);
	size = strlen(ubuf);

	for (i = 0; i < size; i++)
		insert("d", "a", i, &ubuf[i], 1, e, &req);

	for (i = 0; i < size; i++) {
		e++;
		ubuf[i] += 32;
		insert("d", "a", i, &ubuf[i], 1, e, &req);
	}

	memset(fbuf, 0, sizeof(fbuf));
	for (;;) {
		for (i = 0; i < size; i++)
			lookup("d", "a", i, &fbuf[i], 1, e, &req);
		print_message("e = %lu, fbuf = %s\n", e, fbuf);
		assert_string_equal(fbuf, ubuf);
		if (e == 0)
			break;
		e--;
		ubuf[e] -= 32;
	}

	ioreq_fini(&req);
}

/** i/o to variable idx offset */
static void
io_var_idx_offset(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_off_t	 offset;

	/** choose random object */
	obj_random(arg, &oid);

	ioreq_init(&req, oid, arg);

	for (offset = UINT64_MAX; offset > 0; offset >>= 8) {
		char buf[10];

		print_message("idx offset: %lu\n", offset);

		/** Insert */
		insert("var_idx_off_d", "var_idx_off_a", offset, "data",
		       strlen("data") + 1, 0, &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup("var_idx_off_d", "var_idx_off_a", offset, buf, 10, 0,
		       &req);
		assert_int_equal(req.rex.rx_rsize, strlen(buf) + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");
	}

	ioreq_fini(&req);

}

/** i/o to variable akey size */
static void
io_var_akey_size(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_size_t	 size;
	const int	 max_size = 1 << 10;
	char		*key;

	/** akey not supported yet */
	skip();

	/** choose random object */
	obj_random(arg, &oid);

	ioreq_init(&req, oid, arg);

	key = malloc(max_size);
	assert_non_null(key);
	memset(key, 'a', max_size);

	for (size = 1; size <= max_size; size <<= 1) {
		char buf[10];

		print_message("akey size: %lu\n", size);

		/** Insert */
		key[size] = '\0';
		insert("var_akey_size_d", key, 0, "data", strlen("data") + 1, 0,
		       &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup("var_dkey_size_d", key, 0, buf, 10, 0, &req);
		assert_int_equal(req.rex.rx_rsize, strlen("data") + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");
		key[size] = 'b';
	}

	free(key);
	ioreq_fini(&req);
}

/** i/o to variable dkey size */
static void
io_var_dkey_size(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	daos_size_t	 size;
	const int	 max_size = 1 << 10;
	char		*key;

	/** choose random object */
	obj_random(arg, &oid);

	ioreq_init(&req, oid, arg);

	key = malloc(max_size);
	assert_non_null(key);
	memset(key, 'a', max_size);

	for (size = 1; size <= max_size; size <<= 1) {
		char buf[10];

		print_message("dkey size: %lu\n", size);

		/** Insert */
		key[size] = '\0';
		insert(key, "var_dkey_size_a", 0, "data", strlen("data") + 1, 0,
		       &req);

		/** Lookup */
		memset(buf, 0, 10);
		lookup(key, "var_dkey_size_a", 0, buf, 10, 0, &req);
		assert_int_equal(req.rex.rx_rsize, strlen("data") + 1);

		/** Verify data consistency */
		assert_string_equal(buf, "data");
		key[size] = 'b';
	}

	free(key);
	ioreq_fini(&req);
}

/** i/o to variable aligned record size */
static void
io_var_rec_size(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	daos_epoch_t	 epoch;
	struct ioreq	 req;
	daos_size_t	 size;
	const int	 max_size = 1 << 20;
	char		*fetch_buf;
	char		*update_buf;

	/** choose random object */
	obj_random(arg, &oid);

	/** random epoch as well */
	epoch = rand();

	ioreq_init(&req, oid, arg);

	fetch_buf = malloc(max_size);
	assert_non_null(fetch_buf);

	update_buf = malloc(max_size);
	assert_non_null(update_buf);
	memset(update_buf, (rand() % 94) + 33, max_size);

	for (size = 1; size <= max_size; size <<= 1, epoch++) {
		char dkey[30];

		print_message("Record size: %lu val: \'%c\' epoch: %lu\n",
			      size, update_buf[0], epoch);

		/** Insert */
		sprintf(dkey, DF_U64, epoch);
		insert(dkey, "var_rec_size_a", 0, update_buf,
		       size, epoch, &req);

		/** Lookup */
		memset(fetch_buf, 0, max_size);
		lookup(dkey, "var_rec_size_a", 0, fetch_buf,
		       max_size, epoch, &req);
		assert_int_equal(req.rex.rx_rsize, size);

		/** Verify data consistency */
		assert_memory_equal(update_buf, fetch_buf, size);
	}

	free(update_buf);
	free(fetch_buf);
	ioreq_fini(&req);
}

/** very basic update/fetch with data verification */
static void
io_simple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	const char	 dkey[] = "test_update dkey";
	const char	 akey[] = "test_update akey";
	const char	 rec[]  = "test_update record";
	char		*buf;

	obj_random(arg, &oid);

	ioreq_init(&req, oid, arg);

	/** Insert */
	print_message("Insert(e=0)/lookup(e=0)/verify simple kv record\n");

	insert(dkey, akey, 0, (void *)rec, strlen(rec), 0, &req);

	/** Lookup */
	buf = calloc(64, 1);
	assert_non_null(buf);
	lookup(dkey, akey, 0, buf, 64, 0, &req);

	/** Verify data consistency */
	print_message("size = %lu\n", req.rex.rx_rsize);
	assert_int_equal(req.rex.rx_rsize, strlen(rec));
	assert_memory_equal(buf, rec, strlen(rec));
	free(buf);
	ioreq_fini(&req);
}

static void
enumerate(daos_epoch_t epoch, uint32_t *number, daos_key_desc_t *kds,
	  daos_hash_out_t *anchor, char *buf, int len, struct ioreq *req)
{
	int rc;

	daos_iov_set(&req->val_iov, buf, len);
	/** execute fetch operation */
	rc = dsr_obj_list_dkey(req->oh, epoch, number, kds,
			       &req->sgl, anchor,
			       req->arg->async ? &req->ev : NULL);
	assert_int_equal(rc, 0);

	if (req->arg->async) {
		daos_event_t	*evp;

		/** wait for fetch completion */
		rc = daos_eq_poll(req->arg->eq, 1, DAOS_EQ_WAIT, 1, &evp);
		assert_int_equal(rc, 1);
		assert_ptr_equal(evp, &req->ev);
		assert_int_equal(evp->ev_error, 0);
	}
}

/** very basic enumerate */
static void
enumerate_simple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	uint32_t	number = 5;
	daos_key_desc_t kds[5];
	daos_hash_out_t hash_out;
	char *buf;
	char *ptr;
	int total_keys = 0;
	int i;

	obj_random(arg, &oid);

	ioreq_init(&req, oid, (test_arg_t *)*state);

	/** Insert record*/
	print_message("Insert 1000 kv record\n");
	for (i = 0; i < 1000; i++) {
		char dkey[10];

		sprintf(dkey, "%d", i);
		insert(dkey, "a_key", 0, "data", strlen("data") + 1, 0, &req);
	}

	print_message("Enumerate records\n");
	memset(&hash_out, 0, sizeof(hash_out));
	buf = calloc(512, 1);
	/** enumerate records */
	while (!daos_hash_is_eof(&hash_out)) {
		enumerate(0, &number, kds, &hash_out, buf, 512, &req);
		if (number == 0)
			goto next;
		ptr = buf;
		total_keys += number;
		for (i = 0; i < number; i++) {
			char key[32];

			snprintf(key, kds[i].kd_key_len + 1, ptr);
			print_message("i %d key %s len %d\n", i, key,
				     (int)kds[i].kd_key_len);
			ptr += kds[i].kd_key_len;
		}
next:
		if (daos_hash_is_eof(&hash_out))
			break;
		memset(buf, 0, 512);
		number = 5;
	}

	free(buf);
	/** XXX Verify kds */
	ioreq_fini(&req);
	assert_int_equal(total_keys, 1000);
}

/** basic punch test */
static void
punch_simple(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	uint32_t	number = 2;
	daos_key_desc_t kds[2];
	daos_hash_out_t hash_out;
	char		*buf;
	int		total_keys = 0;

	obj_random(arg, &oid);
	ioreq_init(&req, oid, (test_arg_t *)*state);

	/** Insert record*/
	print_message("Insert a few kv record\n");
	insert("punch_test0", "a_key", 0, "data", strlen("data") + 1, 0, &req);
	insert("punch_test1", "a_key", 0, "data", strlen("data") + 1, 0, &req);
	insert("punch_test2", "a_key", 0, "data", strlen("data") + 1, 0, &req);
	insert("punch_test3", "a_key", 0, "data", strlen("data") + 1, 0, &req);
	insert("punch_test4", "a_key", 0, "data", strlen("data") + 1, 0, &req);

	memset(&hash_out, 0, sizeof(hash_out));
	buf = calloc(512, 1);
	/** enumerate records */
	print_message("Enumerate records\n");
	while (number > 0) {
		enumerate(0, &number, kds, &hash_out, buf, 512, &req);
		total_keys += number;
		if (daos_hash_is_eof(&hash_out))
			break;
		number = 2;
	}
	assert_int_equal(total_keys, 5);

	/** punch records */
	print_message("Punch records\n");
	punch("punch_test0", "a_key", 0, 1, &req);
	punch("punch_test1", "a_key", 0, 1, &req);
	punch("punch_test2", "a_key", 0, 1, &req);
	punch("punch_test3", "a_key", 0, 1, &req);
	punch("punch_test4", "a_key", 0, 1, &req);

	memset(&hash_out, 0, sizeof(hash_out));
	/** enumerate records */
	print_message("Enumerate records again\n");
	while (number > 0) {
		enumerate(0, &number, kds, &hash_out, buf, 512, &req);
		total_keys += number;
		if (daos_hash_is_eof(&hash_out))
			break;
		number = 2;
	}
	print_message("get keys %d\n", total_keys);
	free(buf);
	ioreq_fini(&req);
}

static const struct CMUnitTest io_tests[] = {
	{ "DSR200: simple update/fetch/verify",
	  io_simple, async_disable, NULL},
	{ "DSR201: simple update/fetch/verify (async)",
	  io_simple, async_enable, NULL},
	{ "DSR202: i/o with variable rec size",
	  io_var_rec_size, async_disable, NULL},
	{ "DSR203: i/o with variable rec size(async)",
	  io_var_rec_size, async_enable, NULL},
	{ "DSR204: i/o with variable dkey size",
	  io_var_dkey_size, async_enable, NULL},
	{ "DSR205: i/o with variable akey size",
	  io_var_akey_size, async_disable, NULL},
	{ "DSR206: i/o with variable index",
	  io_var_idx_offset, async_enable, NULL},
	{ "DSR207: overwrite in different epoch",
	  io_epoch_overwrite, async_enable, NULL},
	{ "DSR208: simple enumerate", enumerate_simple,
	  async_disable, NULL},
	{ "DSR209: simple punch", punch_simple,
	  async_disable, NULL},
};

static int
setup(void **state)
{
	test_arg_t	*arg;
	int		 rc;

	arg = malloc(sizeof(test_arg_t));
	if (arg == NULL)
		return -1;

	rc = daos_eq_create(&arg->eq);
	if (rc)
		return rc;

	arg->svc.rl_nr.num = 8;
	arg->svc.rl_nr.num_out = 0;
	arg->svc.rl_ranks = arg->ranks;

	arg->hdl_share = false;
	uuid_clear(arg->pool_uuid);
	MPI_Comm_rank(MPI_COMM_WORLD, &arg->myrank);
	MPI_Comm_size(MPI_COMM_WORLD, &arg->rank_size);

	if (arg->myrank == 0) {
		/** create pool with minimal size */
		rc = dmg_pool_create(0731, geteuid(), getegid(), "srv_grp",
				     NULL, "pmem", 256 << 20, &arg->svc,
				     arg->pool_uuid, NULL);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	if (arg->myrank == 0) {
		/** connect to pool */
		rc = dsr_pool_connect(arg->pool_uuid, NULL /* grp */, &arg->svc,
				      DAOS_PC_RW, &arg->poh, &arg->pool_info,
				      NULL /* ev */);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;
	MPI_Bcast(&arg->pool_info, sizeof(arg->pool_info), MPI_CHAR, 0,
		  MPI_COMM_WORLD);

	/** l2g and g2l the pool handle */
	handle_share(&arg->poh, HANDLE_POOL, arg->myrank, arg->poh,
		     HANDLE_SHARE_DSR, 1);

	if (arg->myrank == 0) {
		/** create container */
		uuid_generate(arg->co_uuid);
		rc = dsr_co_create(arg->poh, arg->co_uuid, NULL);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	if (arg->myrank == 0) {
		/** open container */
		rc = dsr_co_open(arg->poh, arg->co_uuid, DAOS_COO_RW, &arg->coh,
				 NULL, NULL);
	}
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	/** l2g and g2l the container handle */
	handle_share(&arg->coh, HANDLE_CO, arg->myrank, arg->poh,
		     HANDLE_SHARE_DSR, 1);

	*state = arg;
	return 0;
}

static int
teardown(void **state) {
	test_arg_t	*arg = *state;
	int		 rc, rc_reduce = 0;

	MPI_Barrier(MPI_COMM_WORLD);

	rc = dsr_co_close(arg->coh, NULL);
	MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
	if (rc_reduce)
		return rc_reduce;

	if (arg->myrank == 0)
		rc = dsr_co_destroy(arg->poh, arg->co_uuid, 1, NULL);
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	rc = dsr_pool_disconnect(arg->poh, NULL /* ev */);
	MPI_Allreduce(&rc, &rc_reduce, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
	if (rc_reduce)
		return rc_reduce;

	if (arg->myrank == 0)
		rc = dmg_pool_destroy(arg->pool_uuid, "srv_grp", 1, NULL);
	MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		return rc;

	rc = daos_eq_destroy(arg->eq, 0);
	if (rc)
		return rc;

	free(arg);
	return 0;
}

int
run_dsr_io_test(int rank, int size)
{
	int rc = 0;

	rc = cmocka_run_group_tests_name("DSR io tests", io_tests,
					 setup, teardown);
	MPI_Barrier(MPI_COMM_WORLD);
	return rc;
}
