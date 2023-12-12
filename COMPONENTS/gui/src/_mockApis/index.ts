import mock from './mockAdapter';

import '@/_mockApis/storage/api-v2-exports';

mock.onAny().passThrough();
