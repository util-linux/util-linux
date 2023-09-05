#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "smartcolsP.h"

static int param_set_data(struct filter_param *n, enum filter_data type, const void *data)
{
	const char *p;

	switch (type) {
	case F_DATA_STRING:
		p = data;
		if (*p == '"') {
			/* remove quotation marks */
			size_t len = strlen(p);

			if (*(p + (len - 1)) == '"')
				len -= 2;
			n->val.str = strndup(p + 1, len);
		} else
			n->val.str = strdup((char *) data);
		if (!n->val.str)
			return -ENOMEM;
		n->has_value = 1;
		break;
	case F_DATA_NUMBER:
		n->val.num = *((unsigned long long *) data);
		n->has_value = 1;
		break;
	case F_DATA_FLOAT:
		n->val.fnum = *((long double *) data);
		n->has_value = 1;
		break;
	case F_DATA_BOOLEAN:
		n->val.boolean = *((bool *) data) == 0 ? 0 : 1;
		n->has_value = 1;
		break;
	default:
		break;
	}
	return 0;
}

struct filter_node *filter_new_param(
		struct libscols_filter *fltr,
		enum filter_data type,
		enum filter_holder holder,
		void *data)
{
	struct filter_param *n = (struct filter_param *) __filter_new_node(
					F_NODE_PARAM,
					sizeof(struct filter_param));
	n->type = type;
	n->holder = holder;
	INIT_LIST_HEAD(&n->pr_params);

	if (param_set_data(n, type, data) != 0)
		return NULL;

	switch (holder) {
	case F_HOLDER_COLUMN:
		n->val.str = strdup((char *) data);
		break;
	default:
		break;
	}

	list_add_tail(&n->pr_params, &fltr->params);

	return (struct filter_node *) n;
}

void filter_free_param(struct filter_param *n)
{
	if (n->type == F_DATA_STRING || n->holder == F_HOLDER_COLUMN)
		free(n->val.str);

	list_del_init(&n->pr_params);
	scols_unref_column(n->col);
	free(n);
}

void filter_dump_param(struct ul_jsonwrt *json, struct filter_param *n)
{
	ul_jsonwrt_object_open(json, "param");

	if (!n->has_value) {
		ul_jsonwrt_value_boolean(json, "has_value", false);
		ul_jsonwrt_value_s(json, "type",
				n->type == F_DATA_STRING ? "string" :
				n->type == F_DATA_NUMBER ? "number" :
				n->type == F_DATA_FLOAT  ? "float" :
				n->type == F_DATA_BOOLEAN ? "bool" :
				"unknown");
	} else {
		switch (n->type) {
		case F_DATA_STRING:
			ul_jsonwrt_value_s(json, "string", n->val.str);
			break;
		case F_DATA_NUMBER:
			ul_jsonwrt_value_u64(json, "number", n->val.num);
			break;
		case F_DATA_FLOAT:
			ul_jsonwrt_value_double(json, "float", n->val.fnum);
			break;
		case F_DATA_BOOLEAN:
			ul_jsonwrt_value_boolean(json, "bool", n->val.boolean);
			break;
		default:
			break;
		}
	}

	switch (n->holder) {
	case F_HOLDER_COLUMN:
		ul_jsonwrt_value_s(json, "column", n->val.str);
		break;
	default:
		break;
	}

	ul_jsonwrt_object_close(json);
}

int filter_eval_param(struct libscols_filter *fltr  __attribute__((__unused__)),
		struct filter_param *n,
		struct libscols_line *ln  __attribute__((__unused__)),
		int *status)
{
	int rc = 0;

	if (!n->has_value) {
		*status = 0;
		goto done;
	}

	switch (n->type) {
	case F_DATA_STRING:
		*status = n->val.str != NULL && *n->val.str != '\0';
		break;
	case F_DATA_NUMBER:
		*status = n->val.num != 0;
		break;
	case F_DATA_FLOAT:
		*status = n->val.fnum != 0.0;
		break;
	case F_DATA_BOOLEAN:
		*status = n->val.boolean != false;
		break;
	default:
		rc = -EINVAL;
		break;
	}
done:
	return rc;
}

int filter_next_param(struct libscols_filter *fltr,
		      struct libscols_iter *itr, struct filter_param **prm)
{
	int rc = 1;

	if (!fltr || !itr || !prm)
		return -EINVAL;
	*prm = NULL;

	if (!itr->head)
		SCOLS_ITER_INIT(itr, &fltr->params);
	if (itr->p != itr->head) {
		SCOLS_ITER_ITERATE(itr, *prm, struct filter_param, pr_params);
		rc = 0;
	}

	return rc;
}

