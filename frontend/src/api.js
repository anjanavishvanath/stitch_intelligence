import axios from 'axios';
import tokenService from './utilities/tokenHelpers';

export const API_BASE_URL = import.meta.env.VITE_API_BASE_URL || "http://localhost:5000";

const api = axios.create({
    baseURL: `${API_BASE_URL}/api`,
    timeout: 10000,
});

// attaching access token to every request
api.interceptors.request.use(
    config => {
    const access = tokenService.getAccessToken();
    if (access) config.headers.Authorization = `Bearer ${access}`;
    return config;
});

// response interceptor: try refreshing token on 401 error and retrying original request
let isRefreshing = false;
let failedQueue = [];

const processQueue = (error, token = null) => {
    failedQueue.forEach(prom => {
        if (error) prom.reject(error);
        else prom.resolve(token);
    });
    failedQueue = [];
};

// Automatic token refresh logic
api.interceptors.response.use(
    res => res,
    async err => {
        const originalRequest = err.config;
        //check if request is 401 (unauthenticated) and ensures the request is not already a retry (to avoid infinite loop)
        if(err.response && err.response.status === 401 && !originalRequest._retry) {
            originalRequest._retry = true;
            // token refresh is already in progress, queue the request
            if(isRefreshing) {
                return new Promise((resolve, reject) => {
                    failedQueue.push({ resolve, reject });
                }).then(token => {
                    originalRequest.headers.Authorization = 'Bearer ' + token;
                    return api(originalRequest);
                }).catch(err => Promise.reject(err));
            }
            isRefreshing = true;
            try {
                const refreshToken = tokenService.getRefreshToken();
                if (!refreshToken) throw new Error('No refresh token available');
                // special inintercepted call to refresh endpoints using raw axios to avoid infinite loop
                const response = await axios.post(`/api/auth/refresh`, {}, {
                    headers: { Authorization: `Bearer ${refreshToken}` },
                    baseURL: API_BASE_URL
                });
                const newAccessToken = response.data.access;
                tokenService.setTokens({ accessToken: newAccessToken });
                // retry all failed request with new token
                processQueue(null, newAccessToken);
                originalRequest.headers.Authorization = 'Bearer ' + newAccessToken;
                return api(originalRequest); //retry original request with new token
            } catch (refreshError) {
                processQueue(refreshError, null); // reject all queued requests
                return Promise.reject(refreshError);
            } finally {
                isRefreshing = false;
            }
        }
        return Promise.reject(err);
    }
);

export default api;