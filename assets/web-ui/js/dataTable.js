/* SPX - A simple profiler for PHP
 * Copyright (C) 2017-2024 Sylvain Lassaut <NoiseByNorthwest@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */



export function makeDataTable(containerId, options, rows) {
    let sort_col = 0;
    let sort_dir = -1;
    let selectionMode = false;
    let selectedKeys = new Set();

    const getRowKey = row => options.getRowKey ? options.getRowKey(row) : row.key;

    function getColumnValue(accessor, row) {
        if ($.type(accessor) === 'function') {
            return accessor(row);
        }

        return row[accessor];
    }

    function notifySelectionChange() {
        if (options.onSelectionChange) {
            options.onSelectionChange(Array.from(selectedKeys));
        }
    }

    let container = $('#' + containerId);
    let render = () => {
        let html = '<table class="data_table text-foreground uk-table uk-table-striped"><thead><tr>';

        if (selectionMode) {
            html += '<th class="p-2" style="text-align: center; vertical-align: middle;" width="1%"></th>';
        }

        for (let i = 0; i < options.columns.length; i++) {
            let column = options.columns[i];
            html += `<th class="p-2 ${i == sort_col ? 'data_table-sort' : ''}" data-col-index="${i}">${column.label}</th>`;
        }

        if (options.rowActions && !selectionMode) {
            html += '<th class="p-2" width="1%"></th>';
        }

        html += '</tr></thead><tbody>';

        rows.sort((a, b) => {
            a = getColumnValue(options.columns[sort_col].value, a);
            b = getColumnValue(options.columns[sort_col].value, b);

            return (a < b ? -1 : (a > b)) * sort_dir;
        });

        for (let row of rows) {
            const key = getRowKey(row);
            let url = options.makeRowUrl && !selectionMode ? options.makeRowUrl(row) : null;
            html += '<tr>';

            if (selectionMode) {
                html += `<td class="p-2" style="text-align: center; vertical-align: middle;"><input type="checkbox" class="uk-checkbox row-select-checkbox" data-key="${key}" ${selectedKeys.has(key) ? 'checked' : ''}></td>`;
            }

            for (let column of options.columns) {
                let value = getColumnValue(column.value, row);
                if (column.format) {
                    value = column.format(value);
                }

                if (url) {
                    value = `<a href="${url}">${value}</a>`;
                }

                html += `<td class="p-2 text-sm ${column.cssClass || ''}">${value}</td>`;
            }

            if (options.rowActions && !selectionMode) {
                html += '<td class="p-2 text-sm" style="white-space: nowrap;">';
                for (let action of options.rowActions) {
                    let title = action.title || action.label || '';
                    html += `<button type="button" class="row-action ${action.cssClass || 'uk-button uk-button-default uk-button-small'}"`
                        + (action.style ? ` style="${action.style}"` : '')
                        + ` title="${title}" aria-label="${title}" data-key="${key}" data-action="${action.name}">`
                        + `${action.html || action.label}</button>`;
                }
                html += '</td>';
            }

            html += '</tr>';
        }

        html += '</tbody></table>';

        container.empty();
        container.append(html);

        container.find('th[data-col-index]').click(e => {
            let current = parseInt($(e.currentTarget).attr('data-col-index'), 10);
            if (sort_col == current) {
                sort_dir *= -1;
            }

            sort_col = current;

            render();
        });

        container.find('.row-select-checkbox').on('change', e => {
            let key = $(e.currentTarget).attr('data-key');
            if (e.currentTarget.checked) {
                selectedKeys.add(key);
            } else {
                selectedKeys.delete(key);
            }

            notifySelectionChange();
        });

        container.find('.row-action').on('click', e => {
            e.preventDefault();

            let key = $(e.currentTarget).attr('data-key');
            let actionName = $(e.currentTarget).attr('data-action');
            let action = options.rowActions.find(a => a.name === actionName);
            let row = rows.find(r => getRowKey(r) === key);

            if (action && row) {
                action.onClick(row);
            }
        });
    }

    render();

    return {
        setSelectionMode(enabled) {
            selectionMode = enabled;
            if (!enabled) {
                selectedKeys.clear();
            }

            notifySelectionChange();
            render();
        },
        getSelectedKeys() {
            return Array.from(selectedKeys);
        },
        removeRows(keys) {
            let keySet = new Set(keys);
            rows = rows.filter(row => !keySet.has(getRowKey(row)));
            for (let key of keySet) {
                selectedKeys.delete(key);
            }

            notifySelectionChange();
            render();
        },
        selectAll() {
            selectedKeys = new Set(rows.map(getRowKey));
            notifySelectionChange();
            render();
        },
        deselectAll() {
            selectedKeys.clear();
            notifySelectionChange();
            render();
        },
        getRowCount() {
            return rows.length;
        },
    };
}
