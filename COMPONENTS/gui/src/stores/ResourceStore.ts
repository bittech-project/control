import { defineStore } from 'pinia';
// project imports
import { ResourceType, type StorageResource } from '@/types/StorageResource';
import { fetchWrapper } from '@/utils/helpers/fetch-wrapper';

interface ResourceStoreState {
    resources_: StorageResource[];
}

export const useResourceStore = defineStore({
    id: 'ResourceStore',

    state: (): ResourceStoreState => ({
        resources_: [],
    }),
    getters: {
        // Get Post from Getters
        resources(state): StorageResource[] {
            return state.resources_;
        },
        exports(state): StorageResource[] {
            return state.resources_.filter((r) => r.type == ResourceType[ResourceType.Export]);
        },
        network(state): StorageResource[] {
            return state.resources_.filter((r) => r.type == ResourceType[ResourceType.Network]);
        },
    },
    actions: {
        async fetch() {
            try {
                this.resources_ = (await fetchWrapper.get('/api/v1/resources')) as StorageResource[];
            } catch (error) {
                console.log(error);
            }
        },
        add(resource: StorageResource) {
            if (this.resources_.find((r) => r.id == resource.id)) {
                Object.assign(<StorageResource>this.resources_.find((r) => r.id == resource.id), resource);
            } else this.resources_.push(resource);
        },
        remove(resource: StorageResource) {
            const res = this.find(resource.id);
            if (res) this.resources_.splice(this.resources_.indexOf(res), 1);
        },
        find(resourceId: string): StorageResource | undefined {
            return this.resources_.find((r) => r.id == resourceId);
        },
    },
});
