/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

const { merge } = require('webpack-merge');

const common = require('./webpack.common.js');

module.exports = merge(common, {
  mode: 'development',
  // No inline source maps: they double the bundle (83 MB vs 41 MB) and the larger app
  // fails to run on the XEM8310 ("Device is not open or no longer available").
  devtool: false,
  devServer: {
    static: './dist',
  },
});
