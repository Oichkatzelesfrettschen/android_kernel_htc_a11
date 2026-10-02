/*
 * The replacement header carries a runtime count of standard hook entries,
 * followed by an error target in the same allocation.
 */

#define xt_alloc_initial_table(type, typ2) ({ \
	unsigned int hook_mask = info->valid_hooks; \
	unsigned int nhooks = hweight32(hook_mask); \
	unsigned int bytes = 0, hooknum = 0, i = 0; \
	struct type##_replace *tbl = kzalloc(sizeof(*tbl) + \
		nhooks * sizeof(struct type##_standard) + \
		sizeof(struct type##_error), GFP_KERNEL); \
	struct type##_standard *entries; \
	struct type##_error *term; \
	if (tbl == NULL) \
		return NULL; \
	entries = (struct type##_standard *)(tbl + 1); \
	term = (struct type##_error *)(entries + nhooks); \
	strncpy(tbl->name, info->name, sizeof(tbl->name)); \
	*term = (struct type##_error)typ2##_ERROR_INIT;  \
	tbl->valid_hooks = hook_mask; \
	tbl->num_entries = nhooks + 1; \
	tbl->size = nhooks * sizeof(struct type##_standard) + \
	                 sizeof(struct type##_error); \
	for (; hook_mask != 0; hook_mask >>= 1, ++hooknum) { \
		if (!(hook_mask & 1)) \
			continue; \
		tbl->hook_entry[hooknum] = bytes; \
		tbl->underflow[hooknum]  = bytes; \
		entries[i++] = (struct type##_standard) \
			typ2##_STANDARD_INIT(NF_ACCEPT); \
		bytes += sizeof(struct type##_standard); \
	} \
	tbl; \
})
