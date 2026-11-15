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
      description: '面向嵌入式 MCU 与上位机的纯 C11 零动态分配通信中间件。',
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
          label: 'Overview',
          translations: { 'zh-CN': '概览' },
          items: [{ autogenerate: { directory: 'overview' } }],
        },
        {
          label: 'Interactive tutorials',
          translations: { 'zh-CN': '渐进式互动教程' },
          items: [{ autogenerate: { directory: 'learn' } }],
        },
        {
          label: 'Schema & WLC guide',
          translations: { 'zh-CN': 'Schema 与编译器指南' },
          items: [{ autogenerate: { directory: 'concepts' } }],
        },
        {
          label: 'Platform & SDK guides',
          translations: { 'zh-CN': '平台接入与上位机 SDK' },
          items: [{ autogenerate: { directory: 'how-to' } }],
        },
        {
          label: 'API & wire reference',
          translations: { 'zh-CN': 'API 与协议参考' },
          items: [{ autogenerate: { directory: 'reference' } }],
        },
        {
          label: 'Development & testing',
          translations: { 'zh-CN': '开发与验证' },
          items: [{ autogenerate: { directory: 'development' } }],
        },
      ],
    }),
  ],
});
