import { DbService } from 'src/db/db.service';
import { ResourceRepository } from './resource.repository';
import { EventEmitter2 } from '@nestjs/event-emitter';

const repo = new ResourceRepository(undefined as DbService, undefined as EventEmitter2);
const checkDuplicated = (repo.checkIdExists = jest.fn());

test('ResourceRepository.makeId generates required id', () => {
    checkDuplicated.mockReturnValueOnce(true);
    checkDuplicated.mockReturnValueOnce(false);
    const id = repo.makeId();
    expect(id).toMatch(/tid_[\d]{2}[a-z]{4}/);
});
