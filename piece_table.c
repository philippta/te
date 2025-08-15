#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct piece {
	unsigned int start;
	unsigned int end;
	struct piece *next;
};

struct piece_table {
	char *buf;
	int size;
	struct piece *piece;
};

void piece_table_load(struct piece_table *pt, char *buf, int size) {
	pt->buf = malloc(size);
	memcpy(pt->buf, buf, size);
	pt->size = size;

	pt->piece = malloc(sizeof(struct piece));
	pt->piece->start = 0;
	pt->piece->end = size;
	pt->piece->next = NULL;
}

void piece_table_insert(struct piece_table *pt, int pos, char *text, int len) {
	int prev_size = pt->size;

	// Grow text and append
	pt->buf = realloc(pt->buf, prev_size + len);
	memcpy(pt->buf + prev_size, text, len);
	pt->size = prev_size + len;

	// TODO: identify the correct piece
	// Split found piece

	struct piece *curr_piece = pt->piece;
	struct piece *split_piece = malloc(sizeof(struct piece));
	struct piece *insert_piece = malloc(sizeof(struct piece));

	split_piece->start = pos;
	split_piece->end = curr_piece->end;
	split_piece->next = NULL;

	curr_piece->next = split_piece;
	curr_piece->end = pos;

	insert_piece->start = prev_size;
	insert_piece->end = prev_size + len;
	insert_piece->next = split_piece;
	curr_piece->next = insert_piece;

	//
	// struct piece *new_piece_before = malloc(sizeof(struct piece));
	// struct piece *new_piece_after = malloc(sizeof(struct piece));
	//
	// new_piece_before->start = prev_size;
	// new_piece_before->end = len;
	// new_piece_before->next = new_piece_after;
	//
	// new_piece_after->start = len;
	// new_piece_after->end = ;
	// new_piece_after->next = NULL;
	//
	// pt->piece->end = pos;
	// pt->piece->next = new_piece_before;
}

void piece_table_print(struct piece_table *pt) {
	for (struct piece *p = pt->piece; p != NULL; p = p->next) {
		write(1, pt->buf + p->start, p->end - p->start);
	}
	write(1, "\n", 1);
}

int main(void) {
	struct piece_table pt = {};
	piece_table_load(&pt, "Helloworld", 11);
	piece_table_print(&pt);
	piece_table_insert(&pt, 5, "fucking", 8);
	piece_table_print(&pt);
}
