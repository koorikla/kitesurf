#!/usr/bin/env bash
# Send messages between coding agents running in tmux panes. See ../SKILL.md.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tmux-msg.sh <command> [args]

  whoami                      Print this pane's id and agent name
  name <name>                 Name this pane so others can address it (e.g. physics)
  list                        List all panes: id, location, agent name, command, directory
  read <target> [lines]       Print the last [lines] (default 60) lines of a pane
  send [-f] <target> <msg>    Type a one-line message into a pane and press Enter

<target> is an agent name set with `name`, a pane id (%3) or session:window.pane.
`send` refuses panes whose foreground program is a shell (the text would run as a
command) unless -f is given, and refuses to send to its own pane.
EOF
}

die() { echo "tmux-msg: $*" >&2; exit 1; }

self_pane() {
    [[ -n "${TMUX_PANE:-}" ]] || die "not inside tmux (TMUX_PANE is unset)"
    echo "$TMUX_PANE"
}

self_name() {
    local pane name
    pane=$(self_pane)
    name=$(tmux display-message -p -t "$pane" '#{@agent}')
    echo "${name:-$pane}"
}

# Turn an agent name, pane id or session:window.pane into a pane id. Matches exactly.
resolve() {
    local target=$1 matches
    matches=$(tmux list-panes -a -F '#{pane_id} #{session_name}:#{window_index}.#{pane_index} #{@agent}' \
        | awk -v t="$target" '$1 == t || $2 == t || $3 == t { print $1 }')
    [[ -n $matches ]] || die "no pane '$target' (see: tmux-msg.sh list)"
    [[ $(wc -l <<<"$matches") -eq 1 ]] || die "several panes match '$target': $(echo $matches)"
    echo "$matches"
}

cmd=${1:-}
[[ $# -gt 0 ]] && shift

case "$cmd" in
    whoami)
        echo "$(self_pane) $(self_name)"
        ;;
    name)
        [[ $# -eq 1 && $1 =~ ^[A-Za-z0-9_.-]+$ ]] || die "usage: name <name> (letters, digits, _ . -)"
        tmux set-option -p -t "$(self_pane)" @agent "$1"
        echo "$(self_pane) $1"
        ;;
    list)
        tmux list-panes -a -F '#{pane_id}	#{session_name}:#{window_index}.#{pane_index}	#{?#{@agent},#{@agent},-}	#{pane_current_command}	#{pane_current_path}'
        ;;
    read)
        [[ $# -ge 1 ]] || die "usage: read <target> [lines]"
        pane=$(resolve "$1")
        lines=${2:-60}
        tmux capture-pane -p -J -t "$pane" -S "-$lines" \
            | awk '{ l[NR] = $0 } NF { last = NR } END { for (i = 1; i <= last; i++) print l[i] }'
        ;;
    send)
        force=0
        if [[ ${1:-} == -f ]]; then force=1; shift; fi
        [[ $# -ge 2 ]] || die "usage: send [-f] <target> <message>"
        pane=$(resolve "$1")
        shift
        msg="$*"
        [[ $msg != *$'\n'* ]] || die "message must be one line; put long text in a file and send its path"
        me=$(self_pane)
        [[ $pane != "$me" ]] || die "refusing to send to your own pane"
        fg=$(tmux display-message -p -t "$pane" '#{pane_current_command}')
        case "$fg" in
            bash|zsh|sh|dash|fish|ksh|tcsh|csh)
                [[ $force -eq 1 ]] || die "pane $pane is running '$fg', not an agent; the message would run as a command (use -f only if you mean it)"
                ;;
        esac
        tmux send-keys -t "$pane" -l -- "[from $(self_name) $me] $msg"
        # Agent TUIs treat fast input as a paste; a separate, later Enter submits it.
        sleep 0.5
        tmux send-keys -t "$pane" Enter
        echo "sent to $pane ($fg)"
        ;;
    ''|-h|--help|help)
        usage
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
