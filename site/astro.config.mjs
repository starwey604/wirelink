// @ts-check
import { defineConfig } from 'astro/config';
import mermaid from 'astro-mermaid';
import starlight from '@astrojs/starlight';

export default defineConfig({
  site: 'https://docs.silkenkite.ink',
  base: '/wirelink',
  trailingSlash: 'always',
  outDir: './dist/wirelink',
  integrations: [
    mermaid({
      autoTheme: true,
      enableLog: false,
    }),
    starlight({
      title: 'Wirelink',
      description:
        'A bounded C11 protocol engine for typed communication over point-to-point links.',
      favicon: '/favicon.svg',
      disable404Route: true,
      customCss: ['./src/styles/custom.css'],
      components: {
        Sidebar: './src/components/Sidebar.astro',
      },
      editLink: {
        baseUrl: 'https://github.com/starwey604/wirelink/edit/main/site/',
      },
      locales: {
        root: {
          label: 'English',
          lang: 'en',
        },
        'zh-cn': {
          label: '简体中文',
          lang: 'zh-CN',
        },
      },
      social: [
        {
          icon: 'github',
          label: 'GitHub',
          href: 'https://github.com/starwey604/wirelink',
        },
      ],
      sidebar: [
        {
          label: 'Tutorials',
          translations: { 'zh-CN': '教程' },
          items: [
            { slug: 'learn/environment-setup' },
            { slug: 'learn/getting-started' },
            { slug: 'learn/request-a-result' },
            { slug: 'learn/save-device-info' },
            { slug: 'learn/async-rpc' },
            { slug: 'learn/deferred-rpc' },
            { slug: 'learn/device-service' },
            { slug: 'learn/external-cmake-project' },
            { slug: 'learn/udp-transport' },
            { slug: 'learn/zephyr-udp-subscriber' },
          ],
        },
        {
          label: 'How-to',
          translations: { 'zh-CN': '操作指南' },
          items: [{ autogenerate: { directory: 'how-to' } }],
        },
        {
          label: 'Concepts',
          translations: { 'zh-CN': '概念' },
          items: [{ autogenerate: { directory: 'concepts' } }],
        },
        {
          label: 'Reference',
          translations: { 'zh-CN': '参考' },
          items: [{ autogenerate: { directory: 'reference' } }],
        },
      ],
    }),
  ],
});
