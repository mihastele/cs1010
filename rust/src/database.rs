//! A deliberately small SQL interface over persistent B+ tree indexed tables.
//! Supported schemas have exactly `id INTEGER PRIMARY KEY, value TEXT`.
use crate::storage::Table;
use std::collections::HashMap;
use std::fs::{self, OpenOptions};
use std::io;
use std::path::{Path, PathBuf};

/// Result of one SQL statement.
#[derive(Debug, PartialEq, Eq)]
pub enum QueryResult {
    Created,
    Inserted,
    Deleted(usize),
    Rows(Vec<(i32, String)>),
}
#[derive(Debug, PartialEq, Eq)]
enum Token {
    Word(String),
    String(String),
    Number(i32),
    Symbol(char),
}
fn invalid(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, message)
}
fn tokenize(sql: &str) -> io::Result<Vec<Token>> {
    let chars: Vec<char> = sql.chars().collect();
    let mut tokens = Vec::new();
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];
        if c.is_whitespace() {
            i += 1;
        } else if matches!(c, '(' | ')' | ',' | '=' | ';' | '*') {
            tokens.push(Token::Symbol(c));
            i += 1;
        } else if c == '\'' {
            i += 1;
            let mut value = String::new();
            let mut closed = false;
            while i < chars.len() {
                if chars[i] == '\'' {
                    if i + 1 < chars.len() && chars[i + 1] == '\'' {
                        value.push('\'');
                        i += 2;
                    } else {
                        i += 1;
                        closed = true;
                        break;
                    }
                } else {
                    value.push(chars[i]);
                    i += 1;
                }
            }
            if !closed {
                return Err(invalid("unterminated string"));
            }
            tokens.push(Token::String(value));
        } else if c.is_ascii_alphabetic() || c == '_' {
            let start = i;
            i += 1;
            while i < chars.len() && (chars[i].is_ascii_alphanumeric() || chars[i] == '_') {
                i += 1;
            }
            tokens.push(Token::Word(
                chars[start..i]
                    .iter()
                    .collect::<String>()
                    .to_ascii_lowercase(),
            ));
        } else if c.is_ascii_digit() || c == '-' {
            let start = i;
            i += 1;
            while i < chars.len() && chars[i].is_ascii_digit() {
                i += 1;
            }
            let n: String = chars[start..i].iter().collect();
            tokens.push(Token::Number(
                n.parse().map_err(|_| invalid("invalid integer"))?,
            ));
        } else {
            return Err(invalid("unexpected character"));
        }
    }
    if tokens.last() == Some(&Token::Symbol(';')) {
        tokens.pop();
    }
    Ok(tokens)
}
struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}
impl Parser {
    fn take(&mut self) -> Option<Token> {
        if self.pos == self.tokens.len() {
            None
        } else {
            let t = std::mem::replace(&mut self.tokens[self.pos], Token::Symbol('\0'));
            self.pos += 1;
            Some(t)
        }
    }
    fn word(&mut self, expected: &str) -> io::Result<()> {
        match self.take() {
            Some(Token::Word(w)) if w == expected => Ok(()),
            _ => Err(invalid(&format!("expected {expected}"))),
        }
    }
    fn name(&mut self) -> io::Result<String> {
        match self.take() {
            Some(Token::Word(w)) if w.len() <= 64 => Ok(w),
            _ => Err(invalid("expected table name (up to 64 characters)")),
        }
    }
    fn symbol(&mut self, c: char) -> io::Result<()> {
        if self.take() == Some(Token::Symbol(c)) {
            Ok(())
        } else {
            Err(invalid(&format!("expected {c}")))
        }
    }
    fn number(&mut self) -> io::Result<i32> {
        if let Some(Token::Number(n)) = self.take() {
            Ok(n)
        } else {
            Err(invalid("expected integer"))
        }
    }
    fn text(&mut self) -> io::Result<String> {
        if let Some(Token::String(s)) = self.take() {
            Ok(s)
        } else {
            Err(invalid("expected quoted string"))
        }
    }
    fn done(&self) -> io::Result<()> {
        if self.pos == self.tokens.len() {
            Ok(())
        } else {
            Err(invalid("unexpected trailing tokens"))
        }
    }
    fn where_id(&mut self) -> io::Result<Option<i32>> {
        if self.pos == self.tokens.len() {
            return Ok(None);
        }
        self.word("where")?;
        self.word("id")?;
        self.symbol('=')?;
        Ok(Some(self.number()?))
    }
}
/// A directory containing one persistent file per table. Only one process may
/// open a database for writing at a time; this implementation does not lock it.
pub struct Database {
    dir: PathBuf,
    tables: HashMap<String, Table>,
}
impl Database {
    pub fn open(dir: impl AsRef<Path>) -> io::Result<Self> {
        fs::create_dir_all(&dir)?;
        Ok(Self {
            dir: dir.as_ref().to_path_buf(),
            tables: HashMap::new(),
        })
    }
    fn path(&self, name: &str) -> PathBuf {
        self.dir.join(format!("table_{name}.csdb"))
    }
    fn table(&mut self, name: &str) -> io::Result<&mut Table> {
        if !self.tables.contains_key(name) {
            let path = self.path(name);
            if !path.is_file() {
                return Err(io::Error::new(
                    io::ErrorKind::NotFound,
                    "table does not exist",
                ));
            }
            self.tables.insert(name.to_owned(), Table::open(path)?);
        }
        Ok(self.tables.get_mut(name).unwrap())
    }
    /// Execute exactly one statement. Keywords and identifiers are case-insensitive.
    pub fn execute(&mut self, sql: &str) -> io::Result<QueryResult> {
        let mut p = Parser {
            tokens: tokenize(sql)?,
            pos: 0,
        };
        match p.take() {
            Some(Token::Word(command)) if command == "create" => {
                p.word("table")?;
                let name = p.name()?;
                p.symbol('(')?;
                p.word("id")?;
                p.word("integer")?;
                p.word("primary")?;
                p.word("key")?;
                p.symbol(',')?;
                p.word("value")?;
                p.word("text")?;
                p.symbol(')')?;
                p.done()?;
                let path = self.path(&name);
                // create_new prevents an existing table from being silently overwritten.
                drop(
                    OpenOptions::new()
                        .write(true)
                        .create_new(true)
                        .open(&path)?,
                );
                self.tables.insert(name, Table::open(path)?);
                Ok(QueryResult::Created)
            }
            Some(Token::Word(command)) if command == "insert" => {
                p.word("into")?;
                let name = p.name()?;
                p.word("values")?;
                p.symbol('(')?;
                let id = p.number()?;
                p.symbol(',')?;
                let value = p.text()?;
                p.symbol(')')?;
                p.done()?;
                if !self.table(&name)?.insert(id, value.as_bytes())? {
                    return Err(io::Error::new(
                        io::ErrorKind::AlreadyExists,
                        "duplicate primary key",
                    ));
                }
                Ok(QueryResult::Inserted)
            }
            Some(Token::Word(command)) if command == "select" => {
                p.symbol('*')?;
                p.word("from")?;
                let name = p.name()?;
                let id = p.where_id()?;
                p.done()?;
                let table = self.table(&name)?;
                let rows = match id {
                    Some(id) => table.range(id, id),
                    None => table.range(i32::MIN, i32::MAX),
                };
                let mut result = Vec::with_capacity(rows.len());
                for (id, value) in rows {
                    result.push((
                        id,
                        String::from_utf8(value.to_vec()).map_err(|_| {
                            io::Error::new(
                                io::ErrorKind::InvalidData,
                                "table contains non-text value",
                            )
                        })?,
                    ));
                }
                Ok(QueryResult::Rows(result))
            }
            Some(Token::Word(command)) if command == "delete" => {
                p.word("from")?;
                let name = p.name()?;
                p.word("where")?;
                p.word("id")?;
                p.symbol('=')?;
                let id = p.number()?;
                p.done()?;
                Ok(QueryResult::Deleted(usize::from(
                    self.table(&name)?.delete(id)?,
                )))
            }
            _ => Err(invalid("unsupported SQL statement")),
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};
    static NEXT: AtomicU64 = AtomicU64::new(0);
    fn path() -> PathBuf {
        std::env::temp_dir().join(format!(
            "cs1010-sql-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ))
    }
    #[test]
    fn sql_and_reopen() {
        let path = path();
        {
            let mut db = Database::open(&path).unwrap();
            assert_eq!(
                db.execute("CREATE TABLE People (id INTEGER PRIMARY KEY, value TEXT);")
                    .unwrap(),
                QueryResult::Created
            );
            db.execute("create table other (id integer primary key, value text)")
                .unwrap();
            db.execute("INSERT INTO people VALUES (2, 'O''Brien')")
                .unwrap();
            db.execute("INSERT INTO people VALUES (-1, 'hello 世界')")
                .unwrap();
            db.execute("INSERT INTO other VALUES (2, 'other')").unwrap();
            assert!(db
                .execute("insert into people values (2, 'duplicate')")
                .is_err());
            assert!(db
                .execute("insert into people values (3, 'unterminated)")
                .is_err());
            assert!(db.execute("delete from people").is_err());
        }
        {
            let mut db = Database::open(&path).unwrap();
            assert_eq!(
                db.execute("select * from PEOPLE").unwrap(),
                QueryResult::Rows(vec![(-1, "hello 世界".into()), (2, "O'Brien".into())])
            );
            assert_eq!(
                db.execute("select * from other where id = 2").unwrap(),
                QueryResult::Rows(vec![(2, "other".into())])
            );
            assert_eq!(
                db.execute("delete from people where id = 2").unwrap(),
                QueryResult::Deleted(1)
            );
            assert_eq!(
                db.execute("delete from people where id = 2").unwrap(),
                QueryResult::Deleted(0)
            );
            assert!(db
                .execute("create table people (id integer primary key, value text)")
                .is_err());
        }
        let mut db = Database::open(&path).unwrap();
        assert_eq!(
            db.execute("select * from people where id = 2").unwrap(),
            QueryResult::Rows(vec![])
        );
        drop(db);
        fs::remove_dir_all(path).unwrap();
    }
}
