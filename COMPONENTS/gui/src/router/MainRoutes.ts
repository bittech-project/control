const MainRoutes = {
    path: '/main',
    meta: {
        requiresAuth: true,
    },
    redirect: '/main',
    component: () => import('@/layouts/full/FullLayout.vue'),
    children: [
        {
            name: 'Index',
            path: '/',
            redirect: '/exports',
        },
        {
            name: 'Exports',
            path: '/exports',
            component: () => import('@/views/exports/index.vue'),
        },
    ],
};

export default MainRoutes;
