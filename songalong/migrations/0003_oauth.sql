-- Sign in with Apple / Google / Facebook: which outside account belongs to which user.
CREATE TABLE IF NOT EXISTS oauth_accounts (
  provider    TEXT NOT NULL,
  subject     TEXT NOT NULL,
  user_id     TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  email       TEXT NOT NULL DEFAULT '',
  created_at  INTEGER NOT NULL,
  PRIMARY KEY (provider, subject)
);
CREATE INDEX IF NOT EXISTS oauth_accounts_user ON oauth_accounts (user_id);
