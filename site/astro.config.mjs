// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

export default defineConfig({
  site: 'https://docs.silkenkite.ink',
  base: '/wirelink',
  trailingSlash: 'always',
  outDir: './dist/wirelink',
  integrations: [
    starlight({
      title: 'Wirelink',
      description:
        'A bounded C11 protocol engine for typed communication over point-to-point links.',
      favicon: '/favicon.svg',
      disable404Route: true,
      customCss: ['./src/styles/custom.css'],
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
          label: 'Learn',
          translations: { 'zh-CN': '学习' },
          items: [
            { slug: 'learn/environment-setup' },
            { slug: 'learn/getting-started' },
            { slug: 'learn/request-a-result' },
            { slug: 'learn/save-device-info' },
          ],
        },
      ],
    }),
  ],
});
