/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import ReactDOM from "react-dom/client";

import "./index.css";

import App from "./App";

const root = ReactDOM.createRoot(document.getElementById("root") as HTMLElement);

// StrictMode is intentionally not used: in development builds it mounts, unmounts and
// remounts <App/>, and the unmount cleanup closes the FrontPanel device that the second
// mount has just reopened ("Device is not open or no longer available").
root.render(<App />);
