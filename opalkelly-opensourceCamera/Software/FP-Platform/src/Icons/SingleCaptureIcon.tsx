/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import * as React from "react";

export interface SingleCaptureIconProps extends React.SVGAttributes<SVGElement> {
    children?: never;
    color?: string;
}

export const SingleCaptureIcon = React.forwardRef<SVGSVGElement, SingleCaptureIconProps>(
    function render(
        { color = "currentColor", ...props },
        forwardedRef,
    ) {
        return (
            <svg
                xmlns="http://www.w3.org/2000/svg"
                width="24"
                height="24"
                viewBox="0 0 24 24"
                fill="none"
                ref={forwardedRef}
                {...props}
            >
                <path d="M12 21C16.9706 21 21 16.9706 21 12C21 7.02941 16.9706 3 12 3C7.02941 3 3 7.02941 3 12C3 16.9706 7.02941 21 12 21Z" stroke="white" stroke-linejoin="round"/>
                <path d="M15.5 10L6.5 5" stroke="white"/>
                <path d="M15.5 14V3.5" stroke="white"/>
                <path d="M12 16L21 11" stroke="white"/>
                <path d="M8.5 14L17.5 19" stroke="white"/>
                <path d="M8.5 10V20.5" stroke="white"/>
                <path d="M12 8L3 13" stroke="white"/>
            </svg>
        );
    },
);

export default SingleCaptureIcon;
