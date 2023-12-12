import { Inject } from '@nestjs/common';
import {
    ValidationArguments,
    ValidationOptions,
    ValidatorConstraint,
    ValidatorConstraintInterface,
    registerDecorator,
} from 'class-validator';
import { ResourceType } from 'src/entities/AbstractStorageResource';
import { ResourceRepository } from '../resource.repository';

@ValidatorConstraint({ async: true })
export class NameAlreadyExistsValidator implements ValidatorConstraintInterface {
    constructor(@Inject('RESOURCE_REPOSITORY') private readonly repo: ResourceRepository) {}

    defaultMessage(validationArguments?: ValidationArguments): string {
        return `name ${validationArguments.value} already exists!`;
    }

    // eslint-disable-next-line @typescript-eslint/no-unused-vars
    validate(name: string, args: ValidationArguments) {
        const [resourceType] = args.constraints;
        return !this.repo.checkDuplicatedName(name, resourceType);
    }
}

export function NameAlreadyExists(t: ResourceType, validationOptions?: ValidationOptions) {
    return function (object: object, propertyName: string) {
        registerDecorator({
            target: object.constructor,
            propertyName: propertyName,
            options: validationOptions,
            constraints: [t],
            validator: NameAlreadyExistsValidator,
        });
    };
}
