#ifndef _STO_SUBSYSTEM_H_
#define _STO_SUBSYSTEM_H_

struct sto_req;

struct sto_subsystem {
	const char *name;

	int (*parse)(struct sto_req *req);
	int (*exec)(struct sto_req *req);
	int (*done)(struct sto_req *req);

	TAILQ_ENTRY(sto_subsystem) list;
};

void sto_add_subsystem(struct sto_subsystem *subsystem);

struct sto_subsystem *sto_subsystem_find(const char *name);


#define STO_SUBSYSTEM_REGISTER(_name)					\
static void __attribute__((constructor)) _name ## _register(void)	\
{									\
	sto_add_subsystem(&_name);					\
}

#endif /* _STO_SUBSYSTEM_H_ */
