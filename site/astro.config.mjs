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
        'An allocation-free C11 endpoint kernel for typed messages and RPC.',
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
        { label: 'Start here', translations: { 'zh-CN': '从这里开始' },
          items: [{ autogenerate: { directory: 'overview' } }] },
        { label: 'Run and learn', translations: { 'zh-CN': '运行与学习' },
          items: [{ autogenerate: { directory: 'learn' } }] },
        { label: 'Understand the kernel', translations: { 'zh-CN': '理解 kernel' },
          items: [{ autogenerate: { directory: 'concepts' } }] },
        { label: 'Integrate', translations: { 'zh-CN': '按需集成' },
          items: [{ autogenerate: { directory: 'how-to' } }] },
        { label: 'API and transport reference', translations: { 'zh-CN': 'API 与传输参考' },
          items: [{ autogenerate: { directory: 'reference' } }] },
        { label: 'Develop and verify', translations: { 'zh-CN': '开发与验证' },
          items: [{ autogenerate: { directory: 'development' } }] },
      ],
    }),
  ],
});
