<script lang="ts" setup>
import CopyableText from '../components/CopyableText.vue';
import { copyClipboard } from '@/utils/Clipboard/copyTextClipboard';
import { onMounted } from 'vue';
import type { StorageResource } from '@/types/StorageResource';
import { useResourceStore } from '@/stores/ResourceStore';
import StartTest from '@/views/exports/StartTest.vue';

const resourceStore = useResourceStore();
onMounted(() => {
    resourceStore.fetch();
});

const healthColor: {
    [key: string]: string;
} = {
    OK: 'success',
    FAILED: 'error',
    DEGRADED: 'warning',
    UNKNOWN: 'grey',
    LOST: 'error',
};
defineProps<{
    exports: StorageResource[];
    envKey: string;
}>();

const emits = defineEmits(['closeMenu']);
</script>

<template>
    <v-table>
        <thead>
            <tr>
                <th class="text-left text-subtitle-1">{{ $t('IQN / NQN / Export Path') }}</th>
                <th class="text-subtitle-1 font-weight-semibold">{{ $t('Health') }}</th>
                <th class="text-subtitle-1 font-weight-semibold">{{ $t('Proto') }}</th>
                <th class="text-subtitle-1 font-weight-semibold">{{ $t('Action') }}</th>
            </tr>
        </thead>
        <tbody>
            <tr v-for="e in exports.filter((e) => e.environment == envKey)">
                <td class="text-subtitle-1">
                    <CopyableText>
                        <template v-slot:copyText>
                            <span style="cursor: pointer" @click="copyClipboard()" class="copy-text">
                                {{ e.state.exportPath }}
                            </span>
                        </template>
                    </CopyableText>
                </td>
                <td class="text-subtitle-1">
                    <v-chip :class="'text-' + healthColor[e.state.health]" label size="small">
                        {{ e.state.health }}
                    </v-chip>
                </td>
                <td class="text-subtitle-1">
                    <span>
                        {{ e.params.proto }}
                    </span>
                </td>
                <td class="text-subtitle-1">
                    <StartTest :environment="envKey" :exports="e"></StartTest>
                </td>
            </tr>
        </tbody>
    </v-table>
</template>
