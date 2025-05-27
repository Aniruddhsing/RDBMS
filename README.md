# RDBMS

A lightweight Relational Database Management System (RDBMS) implemented in C/C++. This project serves as a learning and experimentation platform for database internals, including SQL parsing, query execution, and data storage using a B+ Tree structure.

## Features

- **SQL Parsing:**  
  Custom SQL parser supports `SELECT`, `INSERT INTO`, `CREATE`, and `DROP TABLE` statements.
- **B+ Tree Index:**  
  Efficient indexing and data retrieval through a custom B+ Tree implementation.
- **Data Storage:**  
  In-memory data storage with schema management and support for multiple tables.
- **Query Execution:**  
  Execution plan generation and processing for SQL queries.
- **Command-Line Interface:**  
  Interactive CLI for entering and executing SQL statements.

## Project Structure

- `BPlusTreeLib/`  
  Contains the B+ Tree data structure implementation and related operations.
- `SqlParser/`  
  Implements the SQL parser and CLI for the database system.
- `core/`  
  Core logic for query execution, catalog management, and data manipulation.

## Getting Started

### Build

```sh
cd SqlParser
sh compile.sh
```

### Run

- To run the SQL command-line interface:
  ```sh
  ./SqlParserMain
  ```

- To test the B+ Tree:
  ```sh
  ./BPlusTreeLib/main.exe
  ```

## Example Usage

```
postgres=# CREATE TABLE students (id int, name varchar(32));
postgres=# INSERT INTO students VALUES (1, 'Alice');
postgres=# SELECT * FROM students;
```

## License

MIT License

---

*This project is for educational purposes and not intended for production use.*
