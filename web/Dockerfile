# Interfaz: compila React y la sirve con nginx, que pasa /api al servicio api
FROM node:24-slim AS compilacion
WORKDIR /web
COPY package.json package-lock.json ./
RUN npm ci
COPY . .
RUN npm run build

FROM nginx:alpine
COPY --from=compilacion /web/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf
