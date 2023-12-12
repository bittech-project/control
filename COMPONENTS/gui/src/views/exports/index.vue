<script setup lang="ts">
import { useResourceStore } from '@/stores/ResourceStore';
import ExportsTable from '@/views/exports/ExportsTable.vue';
import { onMounted, ref, computed } from 'vue';

const resourceStore = useResourceStore();
onMounted(() => {
    resourceStore.fetch();
});
let tab = ref(0);

const environmentList = computed(() => {
    return Array.from(new Set(resourceStore.exports.map((obj) => obj.environment)));
});
</script>

<template>
    <v-card>
        <v-tabs bg-color="primary" v-model="tab">
            <v-tab v-for="item in environmentList" :key="item">
                {{ item }}
            </v-tab>
        </v-tabs>
        <v-card-text>
            <v-window v-model="tab">
                <v-window-item v-for="item in environmentList" :key="item">
                    <ExportsTable :envKey="item" :exports="resourceStore.exports" />
                </v-window-item>
            </v-window>
        </v-card-text>
    </v-card>
</template>
