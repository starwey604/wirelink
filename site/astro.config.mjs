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
      description: '写一份 .wl 协议，自动生成单片机 C 代码与上位机 Python / C++ 客户端。',
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
          translations: { 'zh-CN': '开始使用' },
          items: [{ autogenerate: { directory: 'overview' } }],
        },
        {
          label: 'Interactive tutorials',
          translations: { 'zh-CN': '新手实战教程' },
          items: [{ autogenerate: { directory: 'learn' } }],
        },
        {
          label: 'Schema, nodes & WLC',
          translations: { 'zh-CN': '协议与工具指南' },
          items: [{ autogenerate: { directory: 'concepts' } }],
        },
        {
          label: 'Platform & SDK guides',
          translations: { 'zh-CN': '硬件接入与进阶' },
          items: [{ autogenerate: { directory: 'how-to' } }],
        },
        {
          label: 'API & wire reference',
          translations: { 'zh-CN': 'API 与底层参考' },
          items: [{ autogenerate: { directory: 'reference' } }],
        },
        {
          label: 'Development & testing',
          translations: { 'zh-CN': '参与开发' },
          items: [{ autogenerate: { directory: 'development' } }],
        },
      ],
    }),
  ],
});
