const ACCESS_KEY = 'access_token';
const REFRESH_KEY = 'refresh_token';

const tokenService = {
    getAccessToken: () => localStorage.getItem(ACCESS_KEY),
    getRefreshToken: () => localStorage.getItem(REFRESH_KEY),
    setTokens: ({ accessToken, refreshToken }) => {
        if (accessToken) localStorage.setItem(ACCESS_KEY, accessToken);
        if (refreshToken) localStorage.setItem(REFRESH_KEY, refreshToken);
    },
    clearTokens: () => {
        localStorage.removeItem(ACCESS_KEY);
        localStorage.removeItem(REFRESH_KEY);
    }
}

export default tokenService;