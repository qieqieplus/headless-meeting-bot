/** @type {import('tailwindcss').Config} */
export default {
  content: ['./index.html', './src/**/*.{ts,tsx,js,jsx}'],
  theme: {
    extend: {
      colors: {
        night: '#060b19',
        slateglass: 'rgba(15, 23, 42, 0.8)',
      },
      fontFamily: {
        sans: ['Inter', 'system-ui', 'sans-serif'],
      },
      boxShadow: {
        panel: '0 10px 35px rgba(15, 23, 42, 0.45)',
      },
    },
  },
  plugins: [],
}

