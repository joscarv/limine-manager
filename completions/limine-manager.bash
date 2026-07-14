_limine_manager() {
    local cur prev commands
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"
    commands="check-config validate preview show-config status plan diff dry-run apply rollback-status rollback-plan rollback list-backups restore prune-backups"
    case "$prev" in
        --config|--backup) COMPREPLY=( $(compgen -f -- "$cur") ); return ;;
        --log-format) COMPREPLY=( $(compgen -W "text json" -- "$cur") ); return ;;
    esac
    if [[ "$cur" == --* ]]; then
        COMPREPLY=( $(compgen -W "--help --version --config --backup --verbose --log-format" -- "$cur") )
    else
        COMPREPLY=( $(compgen -W "$commands" -- "$cur") )
    fi
}
complete -F _limine_manager limine-manager
