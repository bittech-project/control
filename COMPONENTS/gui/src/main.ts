import { createApp } from 'vue';
import { createPinia } from 'pinia';
import App from './App.vue';
import { router } from './router';
import vuetify from './plugins/vuetify';

import '@/scss/style.scss';
import PerfectScrollbar from 'vue3-perfect-scrollbar';
//Mock Api data
import './_mockApis';

import Maska from 'maska';
//i18
import { createI18n } from 'vue-i18n';
import messages from '@/utils/locales/messages';
const i18n = createI18n({
    locale: 'ru',
    fallbackLocale: 'en',
    messages: messages,
    silentTranslationWarn: true,
    silentFallbackWarn: true,
});

const app = createApp(App);
// fakeBackend();
app.use(router);
app.use(PerfectScrollbar);
app.use(createPinia());
app.use(i18n);
app.use(Maska);
app.use(vuetify).mount('#app');
