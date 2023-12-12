import { fileURLToPath, URL } from 'url';
import { defineConfig, loadEnv } from 'vite';
import vue from '@vitejs/plugin-vue';
import vuetify from 'vite-plugin-vuetify';

// https://vitejs.dev/config/
export default defineConfig(({ command, mode }) => {
    const env = loadEnv(mode, process.cwd(), '');
    return {
        plugins: [
            vue(),
            vuetify({
                autoImport: true,
                styles: { configFile: 'src/scss/variables.scss' },
            }),
        ],
        resolve: {
            alias: {
                '@': fileURLToPath(new URL('./src', import.meta.url)),
            },
        },
        css: {
            preprocessorOptions: {
                scss: {},
            },
        },
        optimizeDeps: {
            exclude: ['vuetify'],
            entries: ['./src/**/*.vue'],
        },
        server: {
            https: {
                key: '../../resources/cert/private-key.pem',
                cert: '../../resources/cert/public-certificate.pem',
            },
            proxy: {
                '/api': {
                    target: env.REMOTE_IP,
                    secure: false,
                },
                '/socket.io': { target: env.REMOTE_IP, ws: true, secure: false },
            },
        },
    };
});
