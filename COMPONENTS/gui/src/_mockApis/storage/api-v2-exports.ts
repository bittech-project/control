import mock from '@/_mockApis/mockAdapter';

mock.onPost('/api/v2/exports').reply((request: any) => {
    const dto = JSON.parse(request.data);
    switch (dto.name) {
        case 'error400': {
            return [
                400,
                {
                    errors: [
                        {
                            error: 'err-001',
                            message: 'something bad happened',
                        },
                        {
                            error: 'err-001',
                            message: 'something bad happened',
                        },
                        {
                            error: 'err-001',
                            message: 'something bad happened',
                        },
                    ],
                },
            ];
        }
        case 'error500': {
            return [
                500,
                {
                    statusCode: 500,
                    message: 'oh sh*t, here we go again!',
                },
            ];
        }
        default: {
            const exportsMock = {
                id: 'tb_1a2b3c4d5e-6f',
                environment: 'environmentDefault',
                name: dto.name,
                type: 'export',
                status: 'NEW',
                created: Date.now(),
                updated: Date.now(),
                params: {
                    relationId: 'tb-1234',
                    proto: 'nfs',
                    exportPath: 'nvme://192.168.87.210:4229/tb_123abc-d4',
                    protoParams: {
                        transport: 'tcp',
                    },
                },
                state: {
                    health: 'OK',
                    locked: false,
                    clients: ['nqn.2023-07.com.tb.nvme.john'],
                },
            };
            return [200, exportsMock];
        }
    }
});
