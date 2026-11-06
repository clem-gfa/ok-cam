/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import * as ToggleGroup from "@radix-ui/react-toggle-group";

import "./ToggleGroupComponent.css";

export class ToggleGroupComponentListItem<T> {
    public readonly label: string;
    public readonly value: T;

    constructor(label: string, value: T) {
        this.label = label;
        this.value = value;
    }
}

export interface ToggleGroupComponentProps<T> {
    selectableItemList: Record<string, ToggleGroupComponentListItem<T>>;
    selectedItemKey: string;
    onSelectedItemChange?: (key: string) => void;
}

function ToggleGroupComponent<T>(props: ToggleGroupComponentProps<T>) {
    const handleValueChange = (value: string | undefined) => {
        if (value && value !== props.selectedItemKey) {
            props.onSelectedItemChange?.(value);
        }
    };

    return (
        <ToggleGroup.Root
            className="okToggleGroup"
            type="single"
            value={props.selectedItemKey}
            onValueChange={handleValueChange}
            >
            {Object.keys(props.selectableItemList).map((key) => (
                <ToggleGroup.Item
                    className="okToggleGroupItem"
                    value={key}
                    key={key}
                >
                    <span>{props.selectableItemList[key]?.label}</span>
                </ToggleGroup.Item>
            ))}
        </ToggleGroup.Root>
    );
}

export default ToggleGroupComponent;
