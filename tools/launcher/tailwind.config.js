/** @type {import('tailwindcss').Config} */
export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {},
  },
  plugins: [
    require("daisyui"),
  ],
  daisyui: {
    themes: [
      {
        "zonai-dark": {
          "primary": "#00e5a3",
          "secondary": "#d4af37",
          "accent": "#38bdf8",
          "neutral": "#1c2421",
          "base-100": "#0f1412",
          "base-200": "#161e1b",
          "base-300": "#222e29",
          "info": "#38bdf8",
          "success": "#00e5a3",
          "warning": "#facc15",
          "error": "#f43f5e",
        },
        "sky-island-light": {
          "primary": "#0d9488",
          "secondary": "#b45309",
          "accent": "#0284c7",
          "neutral": "#e5e0d5",
          "base-100": "#faf8f5",
          "base-200": "#f0ece1",
          "base-300": "#e2dacb",
          "info": "#0284c7",
          "success": "#059669",
          "warning": "#d97706",
          "error": "#e11d48",
        },
      },
    ],
    darkTheme: "zonai-dark",
  },
}
