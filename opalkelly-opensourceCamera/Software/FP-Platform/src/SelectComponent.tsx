/**
 * Copyright (c) 2024-2026 Opal Kelly Incorporated
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import * as Select from "@radix-ui/react-select";

import { ChevronUpIcon, ChevronDownIcon, CheckIcon } from "@radix-ui/react-icons";

import "./SelectComponent.css";

export class SelectComponentListItem<T> {
    public readonly label: string;
    public readonly value: T;

    constructor(label: string, value: T) {
        this.label = label;
        this.value = value;
    }
}

export interface SelectComponentProps<T> {
    selectableItemList: Record<string, SelectComponentListItem<T>>;
    selectedItemKey: string;
    onSelectedItemChange?: (key: string) => void;
}

function SelectComponent<T>(props: SelectComponentProps<T>) {
    return (
        <Select.Root
            value={props.selectedItemKey}
            onValueChange={props.onSelectedItemChange}
            >
            <Select.Trigger className="okSelectTrigger">
                <Select.Value
                    className="okSelectTriggerValue"
                    placeholder="Select Camera Mode..."
                />
                <Select.Icon>
                    <ChevronDownIcon
                        width="24px"
                        color="#FFFFFF"
                    />
                </Select.Icon>
            </Select.Trigger>
            <Select.Portal>
                <Select.Content
                    className="okSelectContent"
                    position="popper"
                    side="bottom"
                    >
                    <Select.ScrollUpButton className="okSelectScrollButton">
                        <ChevronUpIcon />
                    </Select.ScrollUpButton>
                    <Select.Viewport className="okSelectViewPort">
                        {Object.keys(props.selectableItemList).map((key) => (
                            <Select.Item
                                className="okSelectItem"
                                value={key}
                                key={key}
                            >
                                <Select.ItemText>{props.selectableItemList[key]?.label}</Select.ItemText>
                                <Select.ItemIndicator className="okSelectItemIndicator">
                                    <CheckIcon />
                                </Select.ItemIndicator>
                            </Select.Item>
                        ))}
                    </Select.Viewport>
                    <Select.ScrollDownButton className="okSelectScrollButton">
                        <ChevronDownIcon />
                    </Select.ScrollDownButton>
                </Select.Content>
            </Select.Portal>
        </Select.Root>
    );
}

export default SelectComponent;
