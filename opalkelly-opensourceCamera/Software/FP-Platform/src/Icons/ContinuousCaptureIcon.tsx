/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import * as React from "react";

export interface ContinuousCaptureIconProps extends React.SVGAttributes<SVGElement> {
    children?: never;
    color?: string;
}

export const ContinuousCaptureIcon = React.forwardRef<SVGSVGElement, ContinuousCaptureIconProps>(
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
                <path d="M16.5 10.5H3C2.44772 10.5 2 10.9477 2 11.5V19.5C2 20.0523 2.44772 20.5 3 20.5H16.5C17.0523 20.5 17.5 20.0523 17.5 19.5V11.5C17.5 10.9477 17.0523 10.5 16.5 10.5Z" stroke="white"/>
                <path d="M14.5 13.5H10V17.5H14.5V13.5Z" stroke="white" stroke-linecap="round" stroke-linejoin="round"/>
                <path d="M12.5 10C14.433 10 16 8.433 16 6.5C16 4.567 14.433 3 12.5 3C10.567 3 9 4.567 9 6.5C9 8.433 10.567 10 12.5 10Z" stroke="white" stroke-linecap="round" stroke-linejoin="round"/>
                <path d="M6.5 10C7.88071 10 9 8.88071 9 7.5C9 6.11929 7.88071 5 6.5 5C5.11929 5 4 6.11929 4 7.5C4 8.88071 5.11929 10 6.5 10Z" stroke="white" stroke-linecap="round" stroke-linejoin="round"/>
                <path d="M17.5 17.5L22 19.5V11.5L17.5 13.5" stroke="white" stroke-linecap="round" stroke-linejoin="round"/>
            </svg>
        );
    },
);

export default ContinuousCaptureIcon;
