<script lang="ts" setup>
import type { StorageResource } from '@/types/StorageResource';
import { ref, computed } from 'vue';
import { useResourceStore } from '@/stores/ResourceStore';
import { fetchWrapper } from '@/utils/helpers/fetch-wrapper';

const resourceStore = useResourceStore();
let dialog = ref(false);
const props = defineProps<{
    exports: StorageResource;
    environment: string;
}>();

let address = ref('');

function create(target: any, proto: any) {
    const dto = {
        ip: address.value,
        target: target,
        params: {},
    };
    fetchWrapper.post(`/api/v1/exports/${proto}`, dto).then(() => {
        dialog.value = false;
    });
}

const addressesList = computed(() => {
    return resourceStore.network
        .filter((network) => network.environment == props.environment)
        .map((l) => {
            return l.state.addresses[0].split('/')[0];
        });
});
</script>

<template>
    <v-dialog v-model="dialog" persistent max-width="500" min-width="350">
        <template v-slot:activator="{ props }">
            <v-btn variant="outlined" v-bind="props">Action</v-btn>
        </template>
        <v-card class="pt-5">
            <v-card-title>
                <span class="text-h5">Test Setup</span>
            </v-card-title>
            <v-card-text>
                <v-select v-model="address" :items="addressesList"></v-select>
            </v-card-text>
            <v-card-actions>
                <v-spacer></v-spacer>
                <v-btn variant="outlined" @click="dialog = false">Close</v-btn>
                <v-btn variant="outlined" @click="create(exports.state.exportPath, exports.params.proto)">Test</v-btn>
            </v-card-actions>
        </v-card>
    </v-dialog>
</template>
