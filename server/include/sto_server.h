#ifndef _STO_SERVER_H_
#define _STO_SERVER_H_

#define STO_LOCAL_SERVER_ADDR "/var/tmp/sto_server.sock"

int sto_server_start(void);
void sto_server_fini(void);

#endif /* _STO_SERVER_H_ */
