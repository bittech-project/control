<template>
    <v-tooltip v-model="active" text="Copied!" location="top" :open-on-hover="false">
        <template v-slot:activator="{ props }">
            <span @click="popupDisappear()" style="cursor: pointer" v-bind="props"><slot name="copyText"></slot> </span>
        </template>
    </v-tooltip>
</template>

<script>
export default {
    data: () => ({
        active: false,
    }),

    methods: {
        popupDisappear() {
            this.active = true;
            setTimeout(() => {
                this.active = false;
            }, 500);
        },
    },
};
</script>
