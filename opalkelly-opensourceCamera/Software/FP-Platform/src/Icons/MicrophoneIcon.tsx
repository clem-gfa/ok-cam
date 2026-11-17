/**
 * Copyright (c) 2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import * as React from "react";

export interface MicrophoneIconProps extends React.SVGAttributes<SVGElement> {
    children?: never;
    color?: string;
}

export const MicrophoneIcon = React.forwardRef<SVGSVGElement, MicrophoneIconProps>(
    function render({ color = "currentColor", ...props }, forwardedRef) {
        return (
            <svg xmlns="http://www.w3.org/2000/svg" width="18" height="18" viewBox="0 0 18 18" fill="none" ref={forwardedRef} {...props}>
                <rect x="6.25" y="1.25" width="5.5" height="9.5" rx="2.75" stroke={color} strokeWidth="1.2" />
                <path d="M3.5 8.5C3.5 11.5376 5.96243 14 9 14C12.0376 14 14.5 11.5376 14.5 8.5" stroke={color} strokeWidth="1.2" strokeLinecap="round" />
                <path d="M9 14V16.75M6.25 16.75H11.75" stroke={color} strokeWidth="1.2" strokeLinecap="round" />
            </svg>
        );
    },
);

export default MicrophoneIcon;
