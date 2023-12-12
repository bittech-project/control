<script setup lang="ts">
import { RouterView } from 'vue-router';

</script>

<template>
    <v-locale-provider> 
        <v-app>
            <v-main>
                <v-container fluid class="page-wrapper pb-sm-15 pb-10">
                    <div>
                        <RouterView />
                    </div>
                </v-container>
            </v-main>
        </v-app>
    </v-locale-provider>
</template>
