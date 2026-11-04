<?php
// The builtin dispatch table. Keep src/builtins.txt in sync: `make` creates a
// git-<name> shim under libexec/git-core for each entry.

function git_command_table(): array {
    // commands whose implementation file is not present in this snapshot are
    // simply not offered
    return array_filter(git_command_table_all(), 'function_exists');
}

function git_command_table_all(): array {
    return [
        'init' => 'cmd_init',
        'config' => 'cmd_config',
        'hash-object' => 'cmd_hash_object',
        'cat-file' => 'cmd_cat_file',
        'ls-tree' => 'cmd_ls_tree',
        'mktree' => 'cmd_mktree',
        'mktag' => 'cmd_mktag',
        'commit-tree' => 'cmd_commit_tree',
        'var' => 'cmd_var',
        'ls-files' => 'cmd_ls_files',
        'update-index' => 'cmd_update_index',
        'write-tree' => 'cmd_write_tree',
        'read-tree' => 'cmd_read_tree',
        'checkout-index' => 'cmd_checkout_index',
        'update-ref' => 'cmd_update_ref',
        'symbolic-ref' => 'cmd_symbolic_ref',
        'show-ref' => 'cmd_show_ref',
        'for-each-ref' => 'cmd_for_each_ref',
        'check-ref-format' => 'cmd_check_ref_format',
        'reflog' => 'cmd_reflog',
        'rev-parse' => 'cmd_rev_parse',
        'rev-list' => 'cmd_rev_list',
        'merge-base' => 'cmd_merge_base',
        'add' => 'cmd_add',
        'rm' => 'cmd_rm',
        'mv' => 'cmd_mv',
        'status' => 'cmd_status',
        'commit' => 'cmd_commit',
        'diff' => 'cmd_diff',
        'diff-index' => 'cmd_diff_index',
        'diff-files' => 'cmd_diff_files',
        'diff-tree' => 'cmd_diff_tree',
        'log' => 'cmd_log',
        'show' => 'cmd_show',
        'branch' => 'cmd_branch',
        'tag' => 'cmd_tag',
        'checkout' => 'cmd_checkout',
        'reset' => 'cmd_reset',
        'merge' => 'cmd_merge',
        'fsck' => 'cmd_fsck',
        'count-objects' => 'cmd_count_objects',
        'check-ignore' => 'cmd_check_ignore',
        'version' => 'cmd_version',
        'stripspace' => 'cmd_stripspace',
        'upload-pack' => 'cmd_upload_pack',
        'receive-pack' => 'cmd_receive_pack',
        'ls-remote' => 'cmd_ls_remote',
        'clone' => 'cmd_clone',
        'fetch' => 'cmd_fetch',
        'push' => 'cmd_push',
        'remote' => 'cmd_remote',
        'daemon' => 'cmd_daemon',
        'http-backend' => 'cmd_http_backend',
        'index-pack' => 'cmd_index_pack',
        'unpack-objects' => 'cmd_unpack_objects',
        'verify-pack' => 'cmd_verify_pack',
        'bundle' => 'cmd_bundle',
        'show-index' => 'cmd_show_index',
        'sparse-checkout' => 'cmd_sparse_checkout',
        'pack-objects' => 'cmd_pack_objects',
        'repack' => 'cmd_repack',
        'commit-graph' => 'cmd_commit_graph',
        'multi-pack-index' => 'cmd_multi_pack_index',
        'gc' => 'cmd_gc',
        'prune' => 'cmd_prune',
        'pack-refs' => 'cmd_pack_refs',
        'refs' => 'cmd_refs',
        'switch' => 'cmd_switch',
        'restore' => 'cmd_restore',
    ];
}

function cmd_version(array $argv): int {
    out('git version ' . GIT_VERSION . "\n");
    if (in_array('--build-options', $argv, true)) {
        out("cpu: " . php_uname('m') . "\n");
        out("no commit associated with this build\n");
        out("sizeof-long: " . PHP_INT_SIZE . "\n");
        out("sizeof-size_t: " . PHP_INT_SIZE . "\n");
        out("shell-path: /bin/sh\n");
        out("feature: fsmonitor--daemon\n");
        $repo = null;
        $d = Repo::discover();
        out("default-hash: sha1\n");
    }
    return 0;
}
