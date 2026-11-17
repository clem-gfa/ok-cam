/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import * as React from "react";

export interface UserIoIconProps extends React.SVGAttributes<SVGElement> {
    children?: never;
    color?: string;
}

/** An LED (top) above a push button (bottom): the board's user I/O. */
export const UserIoIcon = React.forwardRef<SVGSVGElement, UserIoIconProps>(
    function render({ color = "currentColor", ...props }, forwardedRef) {
        return (
            <svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 18 18" fill="none" ref={forwardedRef} {...props}>
                <circle cx="9" cy="5" r="2.6" fill={color} />
                <path d="M9 0.6V1.6M3.6 2.6L4.3 3.3M14.4 2.6L13.7 3.3M2.4 5H3.4M14.6 5H15.6" stroke={color} strokeWidth="1" strokeLinecap="round" />
                <rect x="3" y="10.5" width="12" height="5.5" rx="2.75" stroke={color} strokeWidth="1.2" />
                <circle cx="9" cy="13.25" r="1.3" fill={color} />
            </svg>
        );
    },
);

export default UserIoIcon;
