import { createContext, useState, useEffect, useContext } from 'react';
import api from '../api';
import tokenService from '../utilities/tokenHelpers';

export const AuthContext = createContext(null);

export const AuthProvider = ({ children }) => {
    const [user, setUser] = useState(null);
    const [loading, setLoading] = useState(true);

    // helper to decode JWT payload safely
    const parseJwt = (token) => {
        try {
            //token =header.payload.signature. get payload part
            const payload = token.split('.')[1];
            return JSON.parse(atob(payload));
        } catch (error) {
            console.error('Error parsing JWT:', error); //remove later
            return null;
        }
    };

    useEffect(() => {
        const accessToken = tokenService.getAccessToken();
        if (accessToken) {
            const payload = parseJwt(accessToken);
            console.log('Decoded JWT payload:', payload); //remove later
            if (payload && payload.sub) {
                setUser({
                    id: parseInt(payload.sub), // check if correct
                    email: payload.email,
                    username: payload.username,
                    role: payload.role,
                    organization: payload.organization
                });
            }
        }
        setLoading(false);
    }, []);

    // Signup, login, logout functions
    const signup = async (username, email, password, role, organization) => {
        await api.post('/auth/signup', { username, email, password, role, organization });
    };

    const login = async (email, password) => {
        const res = await api.post('/auth/login', { email, password });
        tokenService.setTokens({
            accessToken: res.data.access,
            refreshToken: res.data.refresh
        });
        const payload = parseJwt(res.data.access);
        setUser({
            id: parseInt(payload.sub),
            email: payload.email,
            username: payload.username,
            role: payload.role,
            organization: payload.organization
        });
        return res.data;
    };

    const logout = () => {
        tokenService.clearTokens();
        setUser(null);
    };

    return (
        <AuthContext.Provider value={{ user, loading, signup, login, logout }}>
            {children}
        </AuthContext.Provider>
    )
}

// custom hook for easy access to auth context
export const useAuth = () => {
    return useContext(AuthContext);
}