'''
test transformation of weighted regression to normal regression
'''
import sys
sys.path.append('python/')
import numpy as np


def test_weighted_regr():
    '''
    Weighted regression can be solved using cholesky tranform
    and standard regression
    '''
    from numpy import eye
    from numpy import dot
    from numpy.linalg import solve
    from numpy.linalg import cholesky
    from numpy.linalg import inv
    from numpy.random import randn
    from numpy.random import rand

    n = 10
    d = 5
    X = randn(n, d)
    y = randn(n, 1)
    S = 2*eye(n) + 0.2*rand(n, n)  # inverse covar
    S = (S + S.T)/2.0
    W = 5*eye(d) + 0.2*rand(d, d)  # inverse covar of prior
    W = (W + W.T)/2.0
    beta1 = solve(dot(X.T, dot(S, X)) + W, dot(X.T, dot(S, y)))

    # compare with cholesky based soln
    M = inv(cholesky(W).T)
    Xbar = dot(cholesky(S).T, dot(X, M))
    ybar = dot(cholesky(S).T, y)
    beta2_bar = solve(dot(Xbar.T, Xbar) + eye(d), dot(Xbar.T, ybar))
    beta2 = dot(M, beta2_bar)
    assert np.allclose(beta1, beta2)


def test_basis_fnc_second_der():
    ''' Second derivative must match num. derivative'''
    import train_movement_pattern as train
    N = 10
    c = np.linspace(1, 20, N)
    w = 10.0 * np.ones((N,))
    t = np.arange(1, 11)
    h = 1e-6
    num_der2 = np.zeros((N,))
    der2 = np.zeros((N,))
    for i in range(N):
        val_plus = train.basis_fnc_gauss(t[i]+2*h, c[i], w[i])
        val_minus = train.basis_fnc_gauss(t[i]-2*h, c[i], w[i])
        val = train.basis_fnc_gauss(t[i], c[i], w[i])
        num_der2[i] = (val_plus - 2*val + val_minus) / (4*h*h)
        der2[i] = train.basis_fnc_gauss_der2(t[i], c[i], w[i])
    assert np.allclose(num_der2, der2, atol=1e-3)


def test_elastic_net_to_lasso_transform():
    ''' The solutions to elastic net and lasso should be identical
    after transformation
    '''
    import sklearn.linear_model as lm
    import train_movement_pattern as train
    N = 50  # num of samples
    p = 10  # dim of theta
    t = np.linspace(0, 1, N)
    X = train.create_gauss_regressor(p, t, include_intercept=False)
    #_, Xdot2 = train.create_acc_weight_mat(p, t)
    p_hidden = 4  # actual params
    np.random.seed(seed=1)  # 10 passes
    beta = np.vstack((np.random.randn(p_hidden, 1), np.zeros((p-p_hidden, 1))))
    y = np.dot(X, beta) + 0.01 * np.random.randn(N, 1)
    alpha_elastic = 0.3
    ratio = 0.5

    clf = lm.ElasticNet(alpha=alpha_elastic,
                        l1_ratio=ratio, fit_intercept=False)
    clf.fit(X, y)
    beta_hat_1 = clf.coef_

    lamb1 = 2*N*alpha_elastic*ratio
    lamb2 = N*alpha_elastic*(1-ratio)
    y_bar = np.vstack((y, np.zeros((p, 1))))  # if unweighted
    #y_bar = np.vstack((y, np.zeros((N, 1))))
    mult = np.sqrt(1.0/(1+lamb2))
    X_bar = mult * np.vstack((X, np.sqrt(lamb2)*np.eye(p)))  # if unweighted
    #X_bar = mult * np.vstack((X, np.sqrt(lamb2)*Xdot2))
    lamb_bar = lamb1 * mult
    alpha_lasso = lamb_bar/(2*N)

    clf2 = lm.Lasso(alpha=alpha_lasso, fit_intercept=False)
    clf2.fit(X_bar, y_bar)
    beta_hat_2 = clf2.coef_ * mult  # transform back
    print 'Actual param:', beta.T
    print 'Elastic net est:', beta_hat_1
    print 'Lasso est:', beta_hat_2

    #print mult
    # return X
    assert np.allclose(beta_hat_1, beta_hat_2, atol=1e-3)
