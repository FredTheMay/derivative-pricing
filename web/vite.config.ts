import { defineConfig } from 'vitest/config'
import react from '@vitejs/plugin-react'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  server: {
    // Local dev only: the frontend calls /price and /cfa-invariants as relative
    // paths (see src/api.ts), which CloudFront routes to API Gateway in production.
    // This proxies the same paths straight to the deployed API Gateway so `npm run
    // dev` works end-to-end without running the Lambda locally. Not used in the
    // production build (vite build), which is served from CloudFront/S3 with real
    // path-based routing -- see infra/lib/mcd-stack.ts and docs/design/07-aws-demo.md
    // sec.4/5.
    proxy: {
      '/price': 'https://4cpy3vq7l8.execute-api.us-east-2.amazonaws.com',
      '/cfa-invariants': 'https://4cpy3vq7l8.execute-api.us-east-2.amazonaws.com',
    },
  },
  test: {
    environment: 'jsdom',
    setupFiles: ['./src/test/setup.ts'],
    globals: true,
  },
})
