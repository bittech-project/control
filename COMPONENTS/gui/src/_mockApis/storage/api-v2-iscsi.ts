import mock from '@/_mockApis/mockAdapter';
import type { StorageResource } from '@/types/StorageResource';
import { StatePropHealth } from '@/types/BaseResource';

mock.onPost('/api/v2/volumes').reply((request: any) => {
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
            const : StorageResource = {
                id: 'tb_123456789a-zz',
                name: dto.name,
                params: dto.params,
                created: Date.now(),
                updated: Date.now(),
                state: {
                    health: StatePropHealth.OK,
                },
                status: 'OK',
                type: 'volume',
            };
            return [200, ];
        }
    }
});

mock.onDelete(/\/api\/v2\/volumes\/.+/).reply((request) => {
    return [202];
});
